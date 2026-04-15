function renderMermaidBlocks() {
  if (typeof mermaid === "undefined") {
    return;
  }

  mermaid.initialize({
    startOnLoad: false,
    securityLevel: "loose",
    theme: "default"
  });

  const blocks = document.querySelectorAll(
    "pre code.language-mermaid, pre.mermaid code, .mermaid code, .language-text.highlight code"
  );
  let sequence = 0;

  blocks.forEach((block) => {
    const pre = block.parentElement;
    if (!pre || pre.dataset.mermaidProcessed === "true") {
      return;
    }

    const source = block.textContent || "";
    const trimmed = source.trimStart();
    const looksLikeMermaid = /^(flowchart|graph|sequenceDiagram|classDiagram|stateDiagram|erDiagram|journey|gantt|pie|mindmap|timeline|gitGraph)\b/.test(trimmed);

    if (!looksLikeMermaid) {
      return;
    }

    const container = document.createElement("div");
    container.className = "mermaid";
    container.textContent = source;
    container.id = `mermaid-diagram-${sequence++}`;

    pre.dataset.mermaidProcessed = "true";
    pre.replaceWith(container);
  });

  if (document.querySelectorAll(".mermaid").length > 0) {
    mermaid.run({ querySelector: ".mermaid" });
  }
}

if (typeof document$ !== "undefined" && typeof document$.subscribe === "function") {
  document$.subscribe(renderMermaidBlocks);
} else {
  document.addEventListener("DOMContentLoaded", renderMermaidBlocks);
}
