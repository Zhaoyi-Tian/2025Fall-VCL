#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Markdown TF-IDF 统计脚本
用法: python word_processor.py [--top_k N] <md文件1> [md文件2] ...
输出: JSON 格式的 TF-IDF 权重列表
"""

import sys
import os
import json
import re
import jieba
import math
from collections import Counter, defaultdict

# 加载 jieba 停用词表
jieba_stopwords = set()

# 优先加载同目录下的停用词文件（开发环境）
script_dir = os.path.dirname(os.path.abspath(__file__))
local_stop_path = os.path.join(script_dir, 'stop_words.utf8')

# 发布环境路径（从 bin/ 上推到 assets/scripts/）
release_stop_path = os.path.join(script_dir, '..', 'assets', 'scripts', 'stop_words.utf8')
release_stop_path = os.path.normpath(release_stop_path)

# 选择存在的路径
stop_words_path = local_stop_path if os.path.exists(local_stop_path) else release_stop_path
if os.path.exists(stop_words_path):
    with open(stop_words_path, 'r', encoding='utf-8') as f:
        jieba_stopwords = set(line.strip() for line in f if line.strip())


def clean_markdown(raw_md):
    """清洗 Markdown 内容，移除格式标记"""
    text = raw_md
    # 1. 移除代码块 (```...```)
    text = re.sub(r'```.*?```', '', text, flags=re.DOTALL)
    # 2. 移除行内代码 (`...`)
    text = re.sub(r'`.*?`', '', text)
    # 3. 移除 LaTeX 公式
    text = re.sub(r'\$\$[\s\S]*?\$\$', '', text)  # 行间公式
    text = re.sub(r'\$.*?\$', '', text)  # 行内公式
    # 4. 移除图片和链接语法，只保留描述文字内容
    text = re.sub(r'!\[.*?\]\(.*?\)', '', text)  # 删掉图片
    text = re.sub(r'\[(.*?)\]\(.*?\)', r'\1', text)  # 链接保留文字
    # 5. 移除 Markdown 标识符（标题、粗体、斜体等）
    text = re.sub(r'[#*>-]', ' ', text)
    # 6. 移除 HTML 标签
    text = re.sub(r'<.*?>', '', text)
    # 7. 移除多余空白
    text = re.sub(r'\s+', ' ', text)
    return text.strip()

def process_multiple_md(file_paths, top_k=100):
    """读取多个 md 文件，合并后进行 TF-IDF 统计"""
    # 读取所有文档
    docs = []
    for path in file_paths:
        path = os.path.normpath(path)
        with open(path, 'r', encoding='utf-8') as f:
            content = f.read()
            # 清洗 Markdown
            content = clean_markdown(content)
            # jieba 分词并过滤单字符和停用词
            words = [w for w in jieba.cut(content) if len(w) > 1 and w not in jieba_stopwords]
            docs.append(words)

    if not docs:
        return []

    # 计算每个文档的词频 (TF)
    doc_tf = []
    for words in docs:
        tf = Counter(words)
        total = len(words)
        # 归一化 TF
        normalized_tf = {word: count / total for word, count in tf.items()}
        doc_tf.append(normalized_tf)

    # 计算 IDF
    num_docs = len(docs)
    doc_count = defaultdict(int)  # 包含每个词的文档数量
    for words in docs:
        unique_words = set(words)
        for word in unique_words:
            doc_count[word] += 1

    idf = {}
    for word, count in doc_count.items():
        idf[word] = math.log(num_docs / count) + 1  # 加1平滑

    # 计算每个词的 TF-IDF
    word_tfidf = defaultdict(float)
    for tf in doc_tf:
        for word, tf_val in tf.items():
            word_tfidf[word] += tf_val * idf.get(word, 0)

    # 取前 top_k 个高 TF-IDF 词
    sorted_words = sorted(word_tfidf.items(), key=lambda x: x[1], reverse=True)

    result = [
        {"text": word, "weight": round(weight, 6)}
        for word, weight in sorted_words[:top_k]
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
