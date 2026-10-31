"""Run an existing Python test and durably record every executed assertion.

Usage: python recorded_run.py results.jsonl test.py [test arguments ...]
An AST wrapper preserves assertion semantics and logs only after it passes.
"""
import ast
import datetime
import json
import pathlib
import sys

output, filename, *arguments = sys.argv[1:]
source = pathlib.Path(filename).read_text()
stream = open(output, "a", encoding="utf-8", buffering=1)


def record(line, expression, result, detail=""):
    stream.write(json.dumps(dict(time=datetime.datetime.now().isoformat(),
                                file=filename, line=line, assertion=expression,
                                result=result, detail=detail), ensure_ascii=False) + "\n")


class RecordedAssertions(ast.NodeTransformer):
    def visit_Assert(self, node):
        expression = ast.get_source_segment(source, node) or "assert"
        def event(result, detail=None):
            return ast.Expr(ast.Call(ast.Name("__record_assertion", ast.Load()),
                                     [ast.Constant(node.lineno), ast.Constant(expression),
                                      ast.Constant(result), detail or ast.Constant("")], []))
        handler = ast.ExceptHandler(ast.Name("AssertionError", ast.Load()), "__failure",
                                   [event("FAIL", ast.Call(ast.Name("str", ast.Load()),
                                                            [ast.Name("__failure", ast.Load())], [])),
                                    ast.Raise()])
        return ast.copy_location(ast.Try([node], [handler], [event("PASS")], []), node)


tree = ast.fix_missing_locations(RecordedAssertions().visit(ast.parse(source, filename)))
sys.argv = [filename, *arguments]
sys.path.insert(0, str(pathlib.Path(filename).resolve().parent))
try:
    exec(compile(tree, filename, "exec"), {"__name__": "__main__", "__file__": filename,
                                         "__record_assertion": record})
except SystemExit as error:
    if error.code not in (None, 0):
        record(0, "script completion", "ERROR", repr(error))
    raise
except BaseException as error:
    record(0, "script completion", "ERROR", repr(error))
    raise
finally:
    stream.close()
