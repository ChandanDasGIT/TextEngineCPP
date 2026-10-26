#pragma once
#include "languagedef.h"

const LanguageDef& shSyntax();
inline const bool shSyntaxRegistered = registerLanguage(shSyntax());
