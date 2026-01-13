#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Markdown 词频统计脚本
用法: python word_processor.py [--top_k N] <md文件1> [md文件2] ...
输出: JSON 格式的词频列表
"""

import sys
import os
import json
import jieba
from collections import Counter

def process_multiple_md(file_paths, top_k=100):
    """读取多个 md 文件，合并后进行词频统计"""
    combined_content = ""
    for path in file_paths:
        path = os.path.normpath(path)
        with open(path, 'r', encoding='utf-8') as f:
            combined_content += f.read() + "\n"

    # jieba 分词
    words = jieba.cut(combined_content)

    # 过滤单字符
    filtered = [w for w in words if len(w) > 1]

    # 词频统计
    word_counts = Counter(filtered)

    # 取前 top_k 个高频词
    result = [
        {"text": word, "weight": count}
        for word, count in word_counts.most_common(top_k)
    ]

    return result

if __name__ == "__main__":
    args = sys.argv[1:]
    top_k = 100
    file_paths = []

    i = 0
    while i < len(args):
        if args[i] == '--top_k' and i + 1 < len(args):
            top_k = int(args[i + 1])
            i += 2
        else:
            file_paths.append(args[i])
            i += 1

    if file_paths:
        results = process_multiple_md(file_paths, top_k)
        print(json.dumps(results, ensure_ascii=False))
