#/usr/bin/env python3

lines = open("mrgingham.usage").readlines()

lines = [('\"' + it.replace("\\", "\\\\").replace('"', '\\"').rstrip() + '\\n\"\n') for it in lines]
with open("mrgingham.usage.h", "w") as f:
    f.writelines(lines)
