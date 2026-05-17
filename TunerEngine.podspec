require "json"

package = JSON.parse(File.read(File.join(__dir__, "package.json")))

Pod::Spec.new do |s|
  s.name         = "TunerEngine"
  s.version      = package["version"]
  s.summary      = package["description"]
  s.homepage     = package["homepage"]
  s.license      = package["license"]
  s.authors      = package["author"]

  s.platforms    = { :ios => min_ios_version_supported }
  s.source       = { :git => "https://github.com/denizyesilirmak/react-native-tuner-engine.git", :tag => "#{s.version}" }

  s.source_files = [
    "ios/**/*.{h,m,mm}",
    "cpp/src/**/*.cpp",
    "cpp/include/**/*.hpp"
  ]
  s.private_header_files = "ios/**/*.h"

  s.pod_target_xcconfig = {
    "HEADER_SEARCH_PATHS" => "\"$(PODS_TARGET_SRCROOT)/cpp/include\"",
    "CLANG_CXX_LANGUAGE_STANDARD" => "c++17",
    "CLANG_CXX_LIBRARY" => "libc++"
  }

  install_modules_dependencies(s)
end
