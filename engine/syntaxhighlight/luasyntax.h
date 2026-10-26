#pragma once
#include "languagedef.h"

const LanguageDef& luaSyntax();
inline const bool luaSyntaxRegistered = registerLanguage(luaSyntax());
