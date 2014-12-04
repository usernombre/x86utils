from distutils.core import setup
from distutils.extension import Extension
#from Cython.Build import cythonize
from Cython.Distutils import build_ext
from distutils.util import get_platform

if get_platform() == "win32":
    folder = "Win32"
elif get_platform() == "win-amd64":
    folder = "x64"
else:
    assert False # Unsupported platform
    

ext_modules = [Extension("pyx86utils",
                     ["pyx86utils.pyx"],
                     language='c',
                     #define_macros=[('PYREX_WITHOUT_ASSERTIONS', None)],
                     include_dirs=[r'..\x86utils',r'..\libyasm-2.1.2',r'..\udis86-1.7.2'],
                     library_dirs=[r'..\%s\Release' % folder],
                     libraries=['x86utils', 'modules', 'libyasm', 'libudis86']
                     )]
setup(
  name = 'x86 utils',
  cmdclass = {'build_ext': build_ext},
  ext_modules = ext_modules
)
