all: i386 x86_64

i386:
	mkdir -p build_i386
	gcc test_dylib/*.c -dynamiclib -o build_i386/test.dylib -arch i386
	gcc target/*.c -o build_i386/target_app -arch i386
	gcc injector/src/*.c -sectcreate __TEXT __info_plist ./injector/config/Info.plist -o build_i386/dylib_injector -arch i386

x86_64:
	mkdir -p build_x86_64
	gcc test_dylib/*.c -dynamiclib -o build_x86_64/test.dylib -arch x86_64
	gcc target/*.c -o build_x86_64/target_app -arch x86_64
	gcc injector/src/*.c -sectcreate __TEXT __info_plist ./injector/config/Info.plist -o build_x86_64/dylib_injector -arch x86_64
