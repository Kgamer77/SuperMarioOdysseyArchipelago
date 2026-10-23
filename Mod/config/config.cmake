set(LINKFLAGS -nodefaultlibs)
set(LLDFLAGS --no-demangle --gc-sections)

set(OPTIMIZE_OPTIONS_DEBUG -O2 -gdwarf-4)
set(OPTIMIZE_OPTIONS_RELEASE -O3 -ffast-math -flto)
set(WARN_OPTIONS -Werror=return-type -Wno-invalid-offsetof -Wno-macro-redefined -Wno-deprecated-declarations)

set(INCLUDES include lib/OdysseyHeaders/agl)
set(DEFINITIONS IMGUI_DISABLE_TTY_FUNCTIONS IMGUI_DISABLE_FILE_FUNCTIONS)
set(EXCEPTION_FLAGS -fno-exceptions)

set(ASM_OPTIONS "")
set(C_OPTIONS -ffunction-sections -fdata-sections)
set(CXX_OPTIONS "")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

set(IS_32_BIT FALSE)
set(TARGET_IS_STATIC FALSE)
set(MODULE_NAME ${PROJNAME})
set(TITLE_ID 0x0100000000010000)
set(MODULE_BINARY subsdk4)
set(SDK_PAST_1900 FALSE)
set(USE_SAIL TRUE)

set(TRAMPOLINE_POOL_SIZE 0x100)
set(BAKE_SYMBOLS FALSE)

set(HAKKUN_ADDONS Nvn ImGui DebugRenderer HeapSourceDynamic)