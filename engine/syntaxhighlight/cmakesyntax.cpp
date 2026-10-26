#include "cmakesyntax.h"

const LanguageDef& cmakeSyntax() {
    static const LanguageDef d = [] {
        LanguageDef l;
        l.name = "CMake";
        l.extensions = {"cmake"};
        l.fileNames = {"CMakeLists.txt"};
        l.lineComment = "#";
        l.blockOpen = "#[["; l.blockClose = "]]";
        l.quotes = "\"";
        l.multiLineQuotes = "\"";
        l.variableSigil = '$';
        l.caseInsensitive = true;          // keyword lists below are lower case
        l.keywords = {"if","elseif","else","endif","foreach","endforeach","while","endwhile","function",
            "endfunction","macro","endmacro","block","endblock","return","break","continue"};
        l.constants = {"on", "off", "true", "false", "yes", "no"};
        l.builtins = {"set","unset","option","project","cmake_minimum_required","add_executable","add_library",
            "add_subdirectory","add_custom_command","add_custom_target","add_definitions","add_compile_options",
            "add_dependencies","find_package","find_library","find_path","find_program","include",
            "include_directories","target_link_libraries","target_include_directories",
            "target_compile_definitions","target_compile_options","target_sources","target_compile_features",
            "install","file","message","string","list","math","get_filename_component","configure_file",
            "execute_process","set_target_properties","set_property","get_property","enable_testing",
            "add_test","qt_add_executable","qt_add_qml_module","qt_standard_project_setup","qt_add_resources"};
        return l;
    }();
    return d;
}
