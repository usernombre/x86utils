from distutils.core import setup
import pyx86utils_build

setup(
  name = 'x86 utils',
  py_modules=['pyx86utils'],
  ext_modules = [pyx86utils_build.ffi.distutils_extension()],
  install_requires=['cffi']
)
