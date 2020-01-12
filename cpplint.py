import os
import re
import subprocess
import sys
import glob

print("Python {}.{}.{}".format(*sys.version_info))

print("cpplint:")
result = subprocess.run(["cpplint", "--filter=-build/include_subdir,-build/c++11", "--linelength=120", "--root=src", "--recursive", "src"], text=True)
if result.returncode:
  sys.exit(result.returncode)

source_files = glob.glob('src/**/*.*', recursive=True)
cpp_exts = tuple(".c .c++ .cc .cpp .cu .cuh .cxx .h .h++ .hh .hpp .hxx".split())
cpp_files = [file for file in source_files if file.lower().endswith(cpp_exts)]
print(f"{len(cpp_files)} C++ files were found.")

upper_files = [file for file in cpp_files if file != file.lower()]
if upper_files:
  print(f"{len(upper_files)} files contain uppercase characters:")
  print("\n".join(upper_files) + "\n")

space_files = [file for file in cpp_files if " " in file]
if space_files:
  print(f"{len(space_files)} files contain space character:")
  print("\n".join(space_files) + "\n")

bad_files = len(upper_files + space_files)
if bad_files:
  sys.exit(bad_files)

print("OK!")
