# NDK'nin kaldirdigi AndroidNdkModules modulunun yerine gecen dosya.
#
# Telegram'in CMakeLists'i su iki satiri iceriyor:
#
#     include(AndroidNdkModules)
#     android_ndk_import_module_cpufeatures()
#
# Bu modul eskiden NDK'nin build/cmake klasorunde gelirdi. Olculdu: 21.4,
# 25.1, 27.0 ve 29.0 surumlerinin HICBIRINDE artik yok, ama modulun
# derledigi kaynak (sources/android/cpufeatures/cpu-features.c) dordunde
# de yerinde duruyor. Yani eksik olan yalnizca bu sarmalayici.
#
# Telegram kaynagini yamalamak yerine modul CMAKE_MODULE_PATH uzerinden
# disaridan veriliyor; boylece Telegram surumu yukseltildiginde bu dosya
# oldugu gibi calismaya devam ediyor.

function(android_ndk_import_module_cpufeatures)
	if(NOT ANDROID_NDK)
		message(FATAL_ERROR "ANDROID_NDK tanimli degil")
	endif()
	if(TARGET cpufeatures)
		return()
	endif()
	add_library(cpufeatures STATIC
		${ANDROID_NDK}/sources/android/cpufeatures/cpu-features.c)
	target_include_directories(cpufeatures PUBLIC
		${ANDROID_NDK}/sources/android/cpufeatures)
	target_link_libraries(cpufeatures dl)
endfunction()
