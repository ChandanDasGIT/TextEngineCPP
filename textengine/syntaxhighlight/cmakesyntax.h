#pragma once
#include "languagedef.h"

const LanguageDef& cmakeSyntax();
inline const bool cmakeSyntaxRegistered = registerLanguage(cmakeSyntax());
