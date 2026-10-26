#pragma once
#include "languagedef.h"

const LanguageDef& rustSyntax();
inline const bool rustSyntaxRegistered = registerLanguage(rustSyntax());
