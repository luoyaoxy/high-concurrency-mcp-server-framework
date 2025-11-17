#!/usr/bin/env python3

import os
import sys
from typing import Dict, List, Set

def count_lines(file_path: str) -> tuple[int, int, int]:
    """
    统计单个文件的代码行数
    返回：(代码行数, 空行数, 注释行数)
    """
    code_lines = 0
    blank_lines = 0
    comment_lines = 0
    in_block_comment = False

    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                
                # 空行
                if not line:
                    blank_lines += 1
                    continue

                # 处理块注释
                if '/*' in line:
                    in_block_comment = True
                    comment_lines += 1
                    continue

                if '*/' in line:
                    in_block_comment = False
                    comment_lines += 1
                    continue

                if in_block_comment:
                    comment_lines += 1
                    continue

                # 单行注释
                if line.startswith('//'):
                    comment_lines += 1
                    continue

                # 代码行
                code_lines += 1

    except UnicodeDecodeError:
        print(f"Warning: Unable to read {file_path} with UTF-8 encoding")
        return 0, 0, 0
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
        return 0, 0, 0

    return code_lines, blank_lines, comment_lines

def main():
    # 要统计的文件扩展名
    extensions = {'.cc', '.cpp', '.h'}
    
    # 要忽略的目录
    ignore_dirs = {
        '.git',           # git目录
        'build',          # 构建目录
        'deps',           # 依赖目录
        '3rdparty',       # 第三方库
        'third_party',    # 第三方库
        'external',       # 外部库
        'protos',         # 协议文件
        'vendor',         # 供应商代码
        'node_modules',   # npm模块
        '__pycache__',    # Python缓存
        'dist',           # 发布目录
        'out'            # 输出目录
    }
    
    # 统计结果
    total_stats = {
        'files': 0,
        'code_lines': 0,
        'blank_lines': 0,
        'comment_lines': 0
    }
    
    # 按文件类型统计
    extension_stats: Dict[str, Dict[str, int]] = {}
    
    # 遍历当前目录及子目录
    for root, dirs, files in os.walk('.'):
        # 跳过忽略的目录
        dirs[:] = [d for d in dirs if not d.startswith('.') and d not in ignore_dirs]
        
        # 检查当前目录是否在忽略列表中
        current_dir = os.path.basename(root)
        if current_dir in ignore_dirs:
            continue
        
        for file in files:
            ext = os.path.splitext(file)[1].lower()
            if ext not in extensions:
                continue
                
            file_path = os.path.join(root, file)
            
            # 统计单个文件
            code, blank, comment = count_lines(file_path)
            
            # 更新总统计
            total_stats['files'] += 1
            total_stats['code_lines'] += code
            total_stats['blank_lines'] += blank
            total_stats['comment_lines'] += comment
            
            # 更新文件类型统计
            if ext not in extension_stats:
                extension_stats[ext] = {
                    'files': 0,
                    'code_lines': 0,
                    'blank_lines': 0,
                    'comment_lines': 0
                }
            extension_stats[ext]['files'] += 1
            extension_stats[ext]['code_lines'] += code
            extension_stats[ext]['blank_lines'] += blank
            extension_stats[ext]['comment_lines'] += comment
            
            # 打印单个文件的统计
            print(f"{file_path}:")
            print(f"  Code lines: {code}")
            print(f"  Blank lines: {blank}")
            print(f"  Comment lines: {comment}")
            print(f"  Total lines: {code + blank + comment}\n")
    
    # 打印按文件类型统计的结果
    print("\nStatistics by file type:")
    print("=" * 50)
    for ext, stats in extension_stats.items():
        print(f"\n{ext} files:")
        print(f"  Number of files: {stats['files']}")
        print(f"  Code lines: {stats['code_lines']}")
        print(f"  Blank lines: {stats['blank_lines']}")
        print(f"  Comment lines: {stats['comment_lines']}")
        print(f"  Total lines: {stats['code_lines'] + stats['blank_lines'] + stats['comment_lines']}")
    
    # 打印总统计
    print("\nTotal Statistics:")
    print("=" * 50)
    print(f"Total number of files: {total_stats['files']}")
    print(f"Total code lines: {total_stats['code_lines']}")
    print(f"Total blank lines: {total_stats['blank_lines']}")
    print(f"Total comment lines: {total_stats['comment_lines']}")
    print(f"Total lines: {total_stats['code_lines'] + total_stats['blank_lines'] + total_stats['comment_lines']}")

if __name__ == '__main__':
    main() 