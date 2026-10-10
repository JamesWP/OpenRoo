add_library(audio OBJECT
    cdm.cpp
    cdthemes.cpp
    doublesoundbuff.cpp
    soundmanager.cpp
    voicepool.cpp)
target_include_directories(audio PRIVATE
    .)
