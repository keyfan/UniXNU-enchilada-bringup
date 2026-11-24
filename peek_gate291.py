# -*- coding: utf-8 -*-
import io

s = io.open("scripts/check-enchilada-adreno-probe.py", encoding="utf-8").read()
i = s.index("== 901")
seg = s[i:i + 4000]
print("SEGMENT HEAD:")
print(seg[:400])
print("...")
print("SEGMENT TAIL:")
print(seg[-1500:])