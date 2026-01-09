查看ttc文件列表命令：
```bash
fc-query /home/tzy/vcl/vci-2025/assets/fonts/NotoSansCJK-Regular.ttc | 
  grep -E "index:|family:|style:|file:|postscriptname:"
```
拆分文件
```bash
python3 -c "from fontTools.ttLib.ttCollection import TTCollection; import sys, os; ttc = TTCollection(sys.argv[1]); basename = os.path.basename(sys.argv[1]); [font.save(f'{basename}#{i}.ttf') for i, font in enumerate(ttc)]" NotoSansCJK-Regular.ttc
```