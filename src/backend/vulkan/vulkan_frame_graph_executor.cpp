#include "vulkan_frame_graph_executor.hpp"

#include "details/commands/command_buffer.hpp"
#include "details/resource_manager.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace LX_core::backend {
namespace {

void addDiagnostic(FrameGraphExecutionResult &result, std::string message) {
  result.diagnostics.push_back(std::move(message));
}

std::string passNameText(StringID passName) {
  return GlobalStringTable::get().toDebugString(passName);
}

const PreparedFramePassWork *
findPreparedWork(std::span<const PreparedFramePassWork> preparedPasses,
                 StringID passName) {
  const auto it =
      std::find_if(preparedPasses.begin(), preparedPasses.end(),
                   [passName](const PreparedFramePassWork &work) {
                     return work.passName == passName;
                   });
  return it == preparedPasses.end() ? nullptr : &*it;
}

bool targetHasFormat(const RenderTargetDesc &target) {
  return !target.getColorFormats().empty() || target.depthFormat.has_value();
}

bool descNeedsRenderTargetContract(const RenderInputDesc &desc) {
  return desc.pipelineBuildDesc.type == PipelineBuildType::Graphics;
}

bool inputMatchesPipelineType(const RenderInput &input,
                              const RenderInputDesc &desc) {
  switch (desc.pipelineBuildDesc.type) {
  case PipelineBuildType::Graphics:
    return input.kind() == RenderInputKind::Draw;
  case PipelineBuildType::Compute:
    return input.kind() == RenderInputKind::Compute;
  case PipelineBuildType::RayTracing:
    return false;
  }
  return false;
}

void validateGraphPointers(const FrameGraphExecutionRequest &request,
                           FrameGraphExecutionResult &result) {
  if (request.graph == nullptr) {
    addDiagnostic(result, "frame graph is required");
  }
  if (request.compiled == nullptr) {
    addDiagnostic(result, "compiled graph is required");
  }
}

void validateCompiledPasses(const FrameGraphExecutionRequest &request,
                            FrameGraphExecutionResult &result) {
  if (request.graph == nullptr || request.compiled == nullptr) {
    return;
  }
  if (!request.compiled->isValid()) {
    addDiagnostic(result, "compiled graph is invalid: " +
                              request.compiled->errorText());
    return;
  }

  const std::vector<FramePass> &graphPasses = request.graph->getPasses();
  for (const CompiledFrameGraphPass &compiledPass :
       request.compiled->getPasses()) {
    const std::string passName = passNameText(compiledPass.name);
    if (compiledPass.sourcePassIndex >= graphPasses.size()) {
      addDiagnostic(result, "source pass is required for " + passName);
      continue;
    }
    const FramePass &sourcePass = graphPasses[compiledPass.sourcePassIndex];
    if (sourcePass.shaderUri.empty()) {
      addDiagnostic(result, "shader is required for " + passName);
    }
    if (!targetHasFormat(compiledPass.target)) {
      addDiagnostic(result, "target format is required for " + passName);
    }
  }
}

void validatePreparedPasses(const FrameGraphExecutionRequest &request,
                            FrameGraphExecutionResult &result) {
  if (request.compiled == nullptr || !request.compiled->isValid()) {
    return;
  }

  for (const CompiledFrameGraphPass &compiledPass :
       request.compiled->getPasses()) {
    const std::string passName = passNameText(compiledPass.name);
    const PreparedFramePassWork *work =
        findPreparedWork(request.preparedPasses, compiledPass.name);
    if (work == nullptr) {
      addDiagnostic(result, "prepared pass work missing for " + passName);
      continue;
    }

    if (work->descs.size() > work->inputs.size()) {
      addDiagnostic(result, "typed payload is required for " + passName);
    }
    for (usize descIndex = 0; descIndex < work->descs.size(); ++descIndex) {
      const RenderInputDesc &desc = work->descs[descIndex];
      if (desc.pass != compiledPass.name) {
        addDiagnostic(result, "prepared pass contract mismatch for " +
                                  passName);
      }
      if (!desc.accepted()) {
        addDiagnostic(result, "input desc rejected for " + passName);
        continue;
      }
      if (desc.shaderUri.id == 0) {
        addDiagnostic(result, "shader is required for " + passName);
      }
      if (descNeedsRenderTargetContract(desc) &&
          !targetHasFormat(desc.pipelineBuildDesc.target)) {
        addDiagnostic(result, "target format is required for " + passName);
      }
      if (descNeedsRenderTargetContract(desc) &&
          desc.pipelineBuildDesc.target != compiledPass.target) {
        addDiagnostic(result, "prepared pass target mismatch for " + passName);
      }
      if (desc.inputIndex >= work->inputs.size() ||
          !work->inputs[desc.inputIndex]) {
        addDiagnostic(result, "typed payload is required for " + passName);
        continue;
      }

      const RenderInput &input = *work->inputs[desc.inputIndex];
      if (input.pass != compiledPass.name ||
          !inputMatchesPipelineType(input, desc)) {
        addDiagnostic(result, "typed payload is required for " + passName);
      }
    }
  }
}

void validateExecutionTarget(const VulkanFrameGraphExecutionTarget &target,
                             FrameGraphExecutionResult &result) {
  if (target.isPartial()) {
    addDiagnostic(result, "complete Vulkan execution target is required");
  }
}

} // namespace

namespace detail {

void requireResolvedPassContract(const CompiledFrameGraphPass &compiledPass,
                                 const PreparedFramePassWork &work) {
  if (work.passName != compiledPass.name) {
    throw std::runtime_error(
        "prepared pass work does not match compiled pass");
  }
}

void requireResolvedDescContract(const CompiledFrameGraphPass &compiledPass,
                                 const RenderInputDesc &desc) {
  if (desc.pass != compiledPass.name) {
    throw std::runtime_error(
        "prepared input desc does not match compiled pass");
  }
  if (desc.accepted() && descNeedsRenderTargetContract(desc) &&
      desc.pipelineBuildDesc.target != compiledPass.target) {
    throw std::runtime_error(
        "prepared input desc target does not match compiled pass");
  }
}

void requireResolvedInputContract(const CompiledFrameGraphPass &compiledPass,
                                  const RenderInputDesc &desc,
                                  const RenderInput &input) {
  if (input.pass != compiledPass.name) {
    throw std::runtime_error(
        "prepared input payload does not match compiled pass");
  }
  if (!inputMatchesPipelineType(input, desc)) {
    throw std::runtime_error(
        "prepared input payload type does not match pipeline contract");
  }
}

VulkanPreparedFramePassRecordStats recordPreparedFramePassWork(
    const VulkanFrameGraphExecutionTarget &target,
    const CompiledFrameGraphPass &compiledPass,
    const PreparedFramePassWork &work,
    const VulkanPreparedFramePassRecordHooks &hooks) {
  VulkanPreparedFramePassRecordStats stats;
  requireResolvedPassContract(compiledPass, work);
  for (const RenderInputDesc &desc : work.descs) {
    requireResolvedDescContract(compiledPass, desc);
    if (hooks.observeDesc) {
      hooks.observeDesc(desc);
    }
    if (!desc.accepted()) {
      if (hooks.observeRejectedDesc) {
        hooks.observeRejectedDesc(desc);
      }
      continue;
    }
    if (!target.recordsCommands()) {
      continue;
    }

    if (desc.inputIndex >= work.inputs.size() || !work.inputs[desc.inputIndex]) {
      throw std::runtime_error("prepared input payload is missing");
    }
    const RenderInput &input = *work.inputs.at(desc.inputIndex);
    requireResolvedInputContract(compiledPass, desc, input);
    auto pipeline = target.resourceManager->getOrCreatePipeline(desc);
    ++stats.pipelineLookupCount;
    target.commandBuffer->bindPipeline(pipeline);
    target.commandBuffer->bindResources(*target.resourceManager, pipeline,
                                        input, desc);
    ++stats.boundInputCount;
    target.commandBuffer->executeRenderInput(input, desc);
    ++stats.executedInputCount;
  }
  return stats;
}

} // namespace detail

void executePreparedPasses(const FrameGraphExecutionRequest &request,
                           const VulkanFrameGraphExecutionTarget &target,
                           FrameGraphExecutionResult &result) {
  if (request.compiled == nullptr) {
    return;
  }
  try {
    for (const CompiledFrameGraphPass &compiledPass :
         request.compiled->getPasses()) {
      const PreparedFramePassWork *work =
          findPreparedWork(request.preparedPasses, compiledPass.name);
      if (work == nullptr) {
        continue;
      }
      (void)detail::recordPreparedFramePassWork(target, compiledPass, *work);
    }
  } catch (const std::exception &error) {
    addDiagnostic(result, error.what());
  }
}

FrameGraphExecutionResult
VulkanFrameGraphExecutor::execute(const FrameGraphExecutionRequest &request) {
  FrameGraphExecutionResult result;
  validateGraphPointers(request, result);
  validateExecutionTarget(m_executionTarget, result);
  validateCompiledPasses(request, result);
  validatePreparedPasses(request, result);
  if (result.diagnostics.empty()) {
    executePreparedPasses(request, m_executionTarget, result);
  }
  result.ok = result.diagnostics.empty();
  return result;
}

} // namespace LX_core::backend
