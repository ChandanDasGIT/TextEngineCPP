#include "shsyntax.h"

const LanguageDef& shSyntax() {
    static const LanguageDef d = [] {
        LanguageDef l;
        l.name = "shell";
        l.extensions = {"sh", "bash", "zsh", "ksh"};
        l.fileNames = {".bashrc", ".bash_profile", ".bash_aliases", ".profile", ".zshrc", ".zshenv"};
        l.lineComment = "#";
        l.lineCommentAtBoundary = true;
        l.quotes = "\"'";
        l.multiLineQuotes = "\"'";
        l.rawQuotes = "'";
        l.variableSigil = '$';
        l.shebang = true;
        l.callsAreFunctions = false;
        l.keywords = {"if","then","else","elif","fi","for","while","until","do","done","case","esac","in",
            "function","select","time","coproc","return","break","continue","exit"};
        l.builtins = {"echo","printf","cd","pwd","test","read","eval","exec","set","unset","export","local",
            "declare","readonly","typeset","shift","source","alias","unalias","trap","wait","kill","let",
            "getopts","type","command","builtin","true","false"};
        return l;
    }();
    return d;
}
