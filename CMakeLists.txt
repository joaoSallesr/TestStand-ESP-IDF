cmake_minimum_required(VERSION 4.0)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(EXTRA_COMPONENT_DIRS
	"${CMAKE_CURRENT_SOURCE_DIR}/../Components" # my pc
	components
)

project(BancoEstatico-ESP-IDF)