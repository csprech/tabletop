// Builds the C++ side (the bridge library: the device's engine and panel, with JUCE) with CMake, and links it.
const LIB: &str = "tabletop_bridge";
const OPTION: &str = "TABLETOP_BUILD_BRIDGE";
const APP_OPTION: &str = "TABLETOP_BUILD_APP_UNUSED";
const EXTRA_LIBS: &[&str] = &["PadlabAssets", "TabletopAssets"];

fn main() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).parent().unwrap().to_path_buf();
    println!("cargo:rerun-if-env-changed=TABLETOP_DEVICES_DIR");
    for dir in ["src", "bridge"] {
        println!("cargo:rerun-if-changed={}", root.join(dir).display());
    }
    println!("cargo:rerun-if-changed={}", root.join("CMakeLists.txt").display());
    let dst = cmake::Config::new(&root)
        .generator("Ninja")
        .define(OPTION, "ON")
        .define(APP_OPTION, "OFF")
        .define("CMAKE_BUILD_TYPE", "Release")
        .define(
            "TABLETOP_DEVICES_DIR",
            std::env::var("TABLETOP_DEVICES_DIR").unwrap_or_else(|_| root.join("devices").display().to_string()),
        )
        .build_target(LIB)
        .build();
    println!("cargo:rustc-link-search=native={}", dst.join("build").display());
    println!("cargo:rustc-link-lib=static={LIB}");
    for lib in EXTRA_LIBS {
        println!("cargo:rustc-link-lib=static={lib}");
    }
    for fw in [
        "Accelerate", "AppKit", "AudioToolbox", "Carbon", "Cocoa", "CoreAudio", "CoreAudioKit", "CoreFoundation", "CoreGraphics",
        "CoreImage", "CoreMIDI", "CoreServices", "CoreText", "Foundation", "IOKit", "Metal", "MetalKit", "QuartzCore", "Security",
        "UniformTypeIdentifiers", "AVFoundation", "AVKit",
    ] {
        println!("cargo:rustc-link-lib=framework={fw}");
    }
    println!("cargo:rustc-link-lib=dylib=c++");
}
