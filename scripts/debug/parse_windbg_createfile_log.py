'''
在 windbg 调试程序的时候。用如下方法设置断点：

bp KERNELBASE!CreateFileA "r rcx; da @rcx; k L3; g"
bp KERNELBASE!CreateFileW "r rcx; du @rcx; k L3; g"

会在中断的地方打印第一个参数。（这里是文件名字）。本脚本则是为了处理输出的日志，提取对应有效信息。
'''
import re
import argparse
import sys
import os

# 定义系统调用函数的前缀（使用更标准的常量命名）
SYSTEM_CALLS_PREFIX = ['KERNEL32!', 'ntdll!',
                       'ntoskrnl!', 'win32u!', 'WOW64!', 'WOW64T!', 'WOW64CPU!']


def split_blocks(log_text):
  """
  根据 'rcx=' 分隔符将日志文本拆分成独立的块。
  """
  # 使用 re.split，并保留分隔符
  blocks = re.split(r'(rcx=.*)', log_text)
  # 清理和组合：将分隔符与其后的内容组合成一个块
  processed_blocks = []

  for i in range(1, len(blocks), 2):
    if i + 1 < len(blocks):
      processed_blocks.append(blocks[i] + blocks[i + 1])

  return [block.strip() for block in processed_blocks if block.strip()]


def extract_string_from_block(block):
  """
  从一个日志块中提取并拼接双引号内的字符串，形成完整的路径。
  """
  # 正则表达式匹配行首的地址 (例如 0000022a`1b31a090) 后面的双引号内容
  path_pattern = re.compile(
      r'^\s*[\da-fA-F]+`[\da-fA-F]+\s+"(.*?)"$', re.MULTILINE)
  # 查找所有匹配项
  path_segments = path_pattern.findall(block)
  # 拼接所有段落
  string_value = "".join(path_segments)

  return string_value


def extract_rcx_id(block):
  """
  从一个日志块的开头提取 rcx=... 的值作为块的标识。
  """
  # 正则表达式匹配 'rcx=' 后跟至少一个或多个十六进制字符
  rcx_pattern = re.compile(r'(rcx=[\da-fA-F]+)', re.IGNORECASE)
  rcx_match = rcx_pattern.search(block)

  if rcx_match:
    return rcx_match.group(1)
  else:
    return "N/A"


def extract_callsite_info(block):
  """
  从一个日志块中提取 Call Site 信息，并找到第一个非系统调用函数。
  """

  # Call Site 区域通常以 '# Child-SP' 开始
  callstack_start_marker = '# Child-SP'

  if callstack_start_marker not in block:
    return "Call Site region not found."

  # 找到 Call Site 区域的起始位置
  start_index = block.find(callstack_start_marker)
  callstack_section = block[start_index:]

  # 按行分割
  lines = callstack_section.split('\n')

  # 提取 Call Site
  call_sites = []
  # 跳过标题行和可能为空的行
  for line in lines:
    line = line.strip()
    if not line or line.startswith('#'):
      continue

    # 假设 Call Site 是每行最后一个非空的字段（通常是模块!函数名+偏移量）
    parts = line.split()
    if len(parts) >= 4:
      call_site = parts[-1]
      call_sites.append(call_site)

  # 查找第一个非系统调用函数
  for site in call_sites:
    is_system_call = False
    # 排除地址/偏移量部分，只保留函数名（如 igxelpicd64!DumpRegistryKeyDefinitions）
    if '!' not in site:
      continue

    for sys_call in SYSTEM_CALLS_PREFIX:
      if site.startswith(sys_call):
        is_system_call = True
        break

    if not is_system_call:
      return site

  return "Only system calls or no calls found."


def parse_log(log_text):
  """
  主解析函数，协调所有子函数。
  """

  blocks = split_blocks(log_text)
  results = []

  for block in blocks:
    rcx_value = extract_rcx_id(block)
    string_value = extract_string_from_block(block)
    first_non_sys_call = extract_callsite_info(block)

    # 检查是否提取到有效的字符串和 Call Site
    if string_value and first_non_sys_call != "Only system calls or no calls found.":
      results.append({
          "rcx_value": rcx_value,
          "string_value": string_value,
          "first_non_system_call": first_non_sys_call
      })

  return results

# --- 新增命令行参数和主函数 ---


def main():
  """
  程序入口点，处理命令行参数和文件读取。
  """
  parser = argparse.ArgumentParser(
      description="解析 WinDbg 日志文件，提取文件路径和第一个非系统调用 Call Site。"
  )
  # 定义必须传入的日志文件参数
  parser.add_argument(
      "log_file",
      type=str,
      help="要解析的 WinDbg 日志文件路径。"
  )

  # 定义可选的输出文件参数
  parser.add_argument(
      "-o", "--output",
      type=str,
      help="将解析结果写入指定文件（例如 result.txt）。如果未指定，则打印到控制台。"
  )

  args = parser.parse_args()

  log_file_path = args.log_file
  output_file_path = args.output

  if not os.path.exists(log_file_path):
    print(f"错误：文件未找到 -> {log_file_path}", file=sys.stderr)
    sys.exit(1)

  try:
    # 读取日志文件内容，使用 'utf-8' 或 'latin-1' 应对可能的编码问题
    with open(log_file_path, 'r', encoding='utf-8', errors='ignore') as f:
      log_content = f.read()
  except Exception as e:
    print(f"读取文件时发生错误: {e}", file=sys.stderr)
    sys.exit(1)

  # 执行解析
  parsed_results = parse_log(log_content)

  # 格式化输出结果
  output_lines = []
  for i, result in enumerate(parsed_results):
    output_lines.append("-" * 50)
    output_lines.append(f"记录 {i + 1} ({result['rcx_value']})")
    output_lines.append(f"  文件路径/字符串: {result['string_value']}")
    output_lines.append(f"  调用点: {result['first_non_system_call']}")
  output_lines.append("-" * 50)
  output_text = "\n".join(output_lines)

  # 处理输出
  if output_file_path:
    try:
      with open(output_file_path, 'w', encoding='utf-8') as f:
        f.write(output_text)
      print(f"解析结果已成功写入文件: {output_file_path}")
    except Exception as e:
      print(f"写入文件时发生错误: {e}", file=sys.stderr)
      sys.exit(1)
  else:
    # 打印到控制台
    print(output_text)


if __name__ == "__main__":
  main()
