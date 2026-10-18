from pathlib import Path
import subprocess, argparse
parser=argparse.ArgumentParser(description="Build the tested extra graphics dependencies locally for macOS ARM64")
parser.add_argument('--source-directory',type=Path,required=True)
parser.add_argument('--prefix',type=Path,required=True)
a=parser.parse_args()
prefix=a.prefix.resolve(); work=a.source_directory.resolve()
work.mkdir(parents=True,exist_ok=True);prefix.mkdir(parents=True,exist_ok=True)
def run(args):
 print(' '.join(map(str,args)),flush=True)
 subprocess.run(list(map(str,args)),check=True)
def build(name,url,source_sub='',options=()):
 src=work/name; archive=work/(name+'.tar.gz'); b=work/(name+'-build-arm64')
 if not src.exists():
  run(['curl','-fL','--retry','2',url,'-o',archive])
  src.mkdir(); run(['tar','-xf',archive,'-C',src,'--strip-components','1'])
 run(['cmake','-S',src/source_sub,'-B',b,'-G','Ninja','-DCMAKE_OSX_ARCHITECTURES=arm64','-DCMAKE_BUILD_TYPE=Release','-DBUILD_SHARED_LIBS=OFF','-DBUILD_TESTING=OFF','-DCMAKE_INSTALL_PREFIX='+str(prefix),'-DCMAKE_PREFIX_PATH='+str(prefix),*options])
 run(['cmake','--build',b,'--parallel','6']);run(['cmake','--install',b])
build('fmt','https://github.com/fmtlib/fmt/archive/refs/tags/12.0.0.tar.gz',options=['-DFMT_TEST=OFF','-DFMT_DOC=OFF'])
build('robin-map','https://github.com/Tessil/robin-map/archive/refs/tags/v1.4.0.tar.gz')
build('vma','https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.3.0.tar.gz')
build('xxhash','https://github.com/Cyan4973/xxHash/archive/refs/tags/v0.8.3.tar.gz','cmake_unofficial',['-DXXHASH_BUILD_XXHSUM=OFF'])
build('boost','https://github.com/boostorg/boost/releases/download/boost-1.89.0/boost-1.89.0-cmake.tar.xz',options=['-DBOOST_INCLUDE_LIBRARIES=asio;container;container_hash;icl;intrusive;pool;preprocessor','-DBOOST_SKIP_INSTALL_RULES=OFF','-DBOOST_CONTEXT_ARCHITECTURE=arm64','-DBOOST_CONTEXT_ABI=aapcs'])
build('glslang','https://github.com/KhronosGroup/glslang/archive/refs/tags/16.1.0.tar.gz',options=['-DENABLE_OPT=OFF','-DENABLE_GLSLANG_BINARIES=ON','-DBUILD_TESTING=OFF'])
