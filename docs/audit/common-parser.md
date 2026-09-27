# Shared source text parser

`qa_common_parser` and its borrowed-source cursor implement the Q3
`CommonParseState` contract from the TypeScript donor. Game, cgame and renderer
consumers share this implementation. The script preprocessor has a different
token grammar and remains a separate service.

The parser keeps inline token storage and explicit continuation state. It retains
signed-byte whitespace, source line accounting, token overflow behavior, cursor
commit rules, compression, diagnostics and partial matrix writes. Numeric prefix
conversion uses the existing process-lifetime C locale through `qa_parse_atof`.

The author compared the corresponding donor implementation and read its relevant
tests without running them. The Q3 host owner independently reviewed all four
changed parser/numeric files against the complete corresponding donor functions
and found no confirmed defect. Root read the new parser implementation, public
contract and numeric helper, checked storage and borrowing boundaries, and added
the parser to `qa_core`. The compression comment-entry behavior was checked
against `compressCommonText`, including its deliberate difference from token
parsing. No engine compilation, parser execution or tests were run. Complete
B23 and application integration remain open.
