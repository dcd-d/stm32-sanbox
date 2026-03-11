# stm32-sanbox


git submodule add git@github.com:dcd-d/libopencm3.git libopencm3

git add .gitmodules libopencm3
git commit -m "Add libopencm3 submodule at root"


cd libopencm3
make TARGETS=stm32/f1
cd ..