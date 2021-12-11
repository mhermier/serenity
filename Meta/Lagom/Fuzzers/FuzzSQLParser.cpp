/*
 * Copyright (c) 2021, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Discard.h>
#include <LibSQL/AST/Lexer.h>
#include <LibSQL/AST/Parser.h>
#include <stdio.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    auto parser = SQL::AST::Parser(SQL::AST::Lexer({ data, size }));
    discard(parser.next_statement());
    return 0;
}
