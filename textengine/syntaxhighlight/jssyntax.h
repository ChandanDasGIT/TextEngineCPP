#pragma once
#include "languagedef.h"

const LanguageDef& jsSyntax();
inline const bool jsSyntaxRegistered = registerLanguage(jsSyntax());
