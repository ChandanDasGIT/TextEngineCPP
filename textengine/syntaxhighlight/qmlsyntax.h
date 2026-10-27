#pragma once
#include "languagedef.h"

const LanguageDef& qmlSyntax();
inline const bool qmlSyntaxRegistered = registerLanguage(qmlSyntax());
