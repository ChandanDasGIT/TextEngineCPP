#include "rustsyntax.h"

const LanguageDef& rustSyntax() {
    static const LanguageDef d = [] {
        LanguageDef l;
        l.name = "Rust";
        l.extensions = {"rs"};
        l.lineComment = "//";
        l.blockOpen = "/*"; l.blockClose = "*/";
        l.nestedBlockComments = true;
        l.quotes = "\"";
        l.multiLineQuotes = "\"";
        l.rawStrings = true;
        l.lifetimes = true;
        l.attributePrefixes = {"#![", "#["};
        l.capitalizedIsType = true;
        l.macroBang = true;
        l.modifiers = {"pub"};
        l.keywords = {"as","async","await","break","const","continue","crate","dyn","else","enum","extern","fn",
            "for","if","impl","in","let","loop","match","mod","move","mut","pub","ref","return","self","Self",
            "static","struct","super","trait","type","union","unsafe","use","where","while"};
        l.types = {"i8","i16","i32","i64","i128","isize","u8","u16","u32","u64","u128","usize","f32","f64",
            "bool","char","str"};
        l.constants = {"true", "false", "None", "Some", "Ok", "Err"};
        return l;
    }();
    return d;
}
