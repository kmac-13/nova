# Nova Coding Guidelines

These guidelines apply to all code in the Nova, Nova Extras, and Flare libraries.  Every rule here is derived from the existing source; when in doubt, read a neighbouring file and match it.

---

## Table of contents

1. [File structure](#1-file-structure)
2. [Naming](#2-naming)
3. [Braces and indentation](#3-braces-and-indentation)
4. [Spacing](#4-spacing)
5. [Comments](#5-comments)
6. [Classes and structs](#6-classes-and-structs)
7. [Functions and methods](#7-functions-and-methods)
8. [Templates](#8-templates)
9. [Namespaces](#9-namespaces)
10. [Modern C++ features](#10-modern-c-features)
11. [Safety-critical constraints](#11-safety-critical-constraints)
12. [clang-tidy suppressions](#12-clang-tidy-suppressions)
13. [Macros](#13-macros)

---

## 1. File structure

### Header guards

Every header uses both `#pragma once` and a traditional include guard.  The guard name is the file path relative to `include/`, uppercased, with `/` and `.` replaced by `_`.

```cpp
#pragma once
#ifndef KMAC_NOVA_EXTRAS_BOUNDED_COMPOSITE_SINK_H
#define KMAC_NOVA_EXTRAS_BOUNDED_COMPOSITE_SINK_H

// ... content ...

#endif  // KMAC_NOVA_EXTRAS_BOUNDED_COMPOSITE_SINK_H
```

The `#endif` comment uses the same token as the `#ifndef`.

### Header file layout (`.h`)

```
1. #pragma once + include guard
2. project headers  (angle brackets for cross-library, quoted for same library)
3. Qt headers
4. standard library headers
5. namespace
6. class declaration
7. inline / template implementations (if any)
8. closing include guard
```

### Source file layout (`.cpp`)

```
1. matching project header  (quoted)
2. other project headers    (quoted, alphabetical within group)
3. Qt headers               (angle brackets, alphabetical)
4. standard library headers (angle brackets, alphabetical)
```

```cpp
#include "kmac/nova/extras/csv_formatter.h"
#include "kmac/nova/extras/formatting_helper.h"

#include <cstring>
```

Cross-library includes use angle brackets even though they are project headers:

```cpp
#include <kmac/nova/sink.h>          // angle brackets - cross-library
#include <kmac/nova/platform/array.h>

#include "kmac/nova/extras/buffer.h" // quoted - same library
```

### Prefer explicit includes

Include exactly what you use. Do not rely on transitive includes from other project headers.

---

## 2. Naming

| Entity | Convention | Example |
|---|---|---|
| Class / struct | PascalCase | `BoundedCompositeSink` |
| Method / free function | camelCase | `formatAndWrite` |
| Local variable | camelCase | `bytesWritten` |
| Parameter | camelCase | `maxSinks` |
| Private member | leading `_` + camelCase | `_count`, `_formatter` |
| Constant / enumerator | `UPPER_SNAKE_CASE` | `MAX_BUFFER_SIZE` |
| `extern` variable | leading `ext_` + camelCase | `ext_frameCounter` |
| Template parameter | PascalCase | `BufferSize`, `TagType` |
| Namespace | lowercase | `kmac`, `nova`, `extras` |
| File | `snake_case.h` / `snake_case.cpp` | `bounded_composite_sink.h` |

Abbreviations follow the same casing rules as full words - `fnv1a` not `FNV1A`, `ISO8601Formatter` not `Iso8601Formatter`.

---

## 3. Braces and indentation

**Indentation:** tabs, never spaces.

**Brace style:** Allman (opening brace on its own line) for everything *except* namespaces and lambdas, which use same-line braces.

```cpp
// correct - Allman for class
class TruncatingBuffer
{
public:
	bool append( const char* data, std::size_t length ) noexcept;
};

// correct - Allman for function definition
bool TruncatingBuffer::append( const char* data, std::size_t length ) noexcept
{
	if ( length == 0 )
	{
		return false;
	}
	// ...
}

// correct - namespaces do not get Allman braces
namespace kmac {
namespace nova {
namespace extras {

} // namespace extras
} // namespace nova
} // namespace kmac
```

**Single-statement bodies always have braces:**

```cpp
// correct
if ( _size >= _capacity )
{
	return false;
}

// wrong - brace omitted
if ( _size >= _capacity )
	return false;
```

This applies to `if`, `else`, `for`, `while`, and `do`.

---

## 4. Spacing

**Space after control-flow keywords** (`if`, `for`, `while`, `switch`):

```cpp
if ( condition )
for ( std::size_t i = 0; i < _count; ++i )
while ( ! done )
switch ( _stage )
```

**Space inside parentheses** for control flow and function calls:

```cpp
// correct
if ( _count >= MaxSinks || contains( sink ) )
buf.append( data, length )

// wrong
if(_count >= MaxSinks)
buf.append(data, length)
```

**Space inside square brackets and angle brackets:**

```cpp
_sinks[ i ]
platform::Array< char, BufferSize >
```

**No alignment padding** in struct member declarations or initialiser lists:

```cpp
// correct
Record r {};
r.tag = "FILESINK";
r.tagId = 0;
r.file = "test.cpp";
r.messageSize = static_cast< std::uint32_t >( len );

// wrong - aligned with spaces
r.tag         = "FILESINK";
r.tagId       = 0;
r.file        = "test.cpp";
r.messageSize = static_cast< std::uint32_t >( len );
```

The same rule applies to constructor initialiser lists:

```cpp
// correct
FormattingFileSink( FILE* file, Formatter* formatter ) noexcept
	: _file( file )
	, _formatter( formatter )
{
}
```

Comment alignment padding is okay:

```cpp
FILE* _file = nullptr;            ///< output file (not owned, must remain valid)
Formatter* _formatter = nullptr;  ///< formatter (optional, not owned)
```

**Pointer and reference declarators** attach to the type:

```cpp
const char* data;
Sink& sink;
```

---

## 5. Comments

### Inline comment style

Comments start lowercase unless the word is a proper noun or type name.  Hyphens, not em dashes.  No trailing period on single-line comments.  Use the serial (Oxford) comma in lists of three or more items.

```cpp
// correct - explains why, not what
// null fields produce unquoted empty fields - no quoting check needed

// wrong — em dash instead of hyphen (-)
// null fields produce unquoted empty fields — no quoting check needed

// wrong - sentence case
// Null fields produce unquoted empty fields - no quoting check needed

// wrong - describes what the code obviously does
// set _truncated to true
_truncated = true;
```

### Doxygen

Public API uses Doxygen.  `@brief` is one sentence.  `@param`, `@return`, and `@note` entries follow on separate lines.

```cpp
/**
 * @brief Add a child sink if capacity allows.
 *
 * @param sink sink to add (must remain valid)
 * @return true if added successfully, false if at capacity
 *
 * @note sink is not owned (caller manages lifetime)
 * @note sink is added to end (maintains insertion order)
 */
[[nodiscard]]
bool add( kmac::nova::Sink& sink ) noexcept;
```

Class-level doc comments include thread safety, heap allocation status, and a usage example.  Safety-critical classes carry an explicit badge:

```cpp
/**
 * @brief Fixed-capacity composite sink with compile-time maximum.
 *
 * SAFE FOR SAFETY-CRITICAL SYSTEMS
 *
 * ...
 *
 * Thread safety:
 * - not thread-safe (wrap with SynchronizedSink if needed)
 *
 * Usage:
 *   BoundedCompositeSink<4> multi;
 *   multi.add( console );
 */
```

### What not to put in comments

- No references to cyclomatic or cognitive complexity scores.
- No "refactoring note" or "TODO: simplify" markers unless tracked elsewhere.
- No commented-out code.

---

## 6. Classes and structs

### Declaration order

```
private:
	member variables   (leading underscore)

public:
	constructors
	destructor
	public methods

private:
	private helper methods
```

Static members and methods may be grouped with their logical counterparts rather than strictly following the public/private split when it aids clarity.

### Ownership and lifetime

Non-owning pointers are raw pointers.  Owning resources use RAII.  Document lifetime requirements in `@note`:

```cpp
FILE* _file = nullptr;            ///< output file (not owned, must remain valid)
Formatter* _formatter = nullptr;  ///< formatter (optional, not owned)
```

### `[[nodiscard]]`

Apply to any method whose return value encodes success/failure or a resource that must not be silently discarded:

```cpp
[[nodiscard]]
bool add( kmac::nova::Sink& sink ) noexcept;

[[nodiscard]]
bool remove( kmac::nova::Sink& sink ) noexcept;

[[nodiscard]]
bool contains( const kmac::nova::Sink& sink ) const noexcept;
```

### `noexcept`

Every `process()` override is `noexcept`.  Apply `noexcept` to any function that genuinely cannot throw - constructors taking POD arguments, accessors, and arithmetic helpers.  Never add `noexcept` speculatively to functions that call potentially-throwing stdlib operations.

```cpp
void process( const kmac::nova::Record& record ) noexcept override;
std::size_t size() const noexcept;
std::size_t capacity() const noexcept;
```

---

## 7. Functions and methods

**Prefer `const` member functions** wherever state is not mutated.

**Prefer early return** over deeply nested `if`/`else`:

```cpp
// correct
bool TruncatingBuffer::append( const char* data, std::size_t length ) noexcept
{
	if ( length == 0 )
	{
		return false;
	}

	if ( _size + length > _capacity )
	{
		_truncated = true;
		length = _capacity - _size;
	}

	std::memcpy( _buffer + _size, data, length );
	_size += length;
	return true;
}
```

**Extract private helpers** when a function exceeds the cognitive complexity threshold (15) rather than suppressing the clang-tidy check.  The `NOLINT` suppression is a last resort, e.g. for state-machine `switch` blocks, where extraction would harm readability.

**Free functions in anonymous namespaces** for file-local helpers - not `static`:

```cpp
namespace {

bool needsEscaping( char c ) noexcept
{
	return c == '<' || c == '>' || c == '&' || c == '"';
}

} // namespace
```

---

## 8. Templates

Template parameters are PascalCase.  Single-line `template<>` declarations with spaces inside angle brackets:

```cpp
template< std::size_t MaxSinks >
class BoundedCompositeSink final : public kmac::nova::Sink
{
	// ...
};

template< std::size_t MaxSinks >
bool BoundedCompositeSink< MaxSinks >::add( kmac::nova::Sink& sink ) noexcept
{
	// ...
}
```

**`NOVA_IF_CONSTEXPR` not `if constexpr` directly** in macro bodies, to preserve C++11/14 compatibility:

```cpp
// correct - uses the portability wrapper
#define NOVA_LOG_STREAM( TagType ) \
	NOVA_IF_CONSTEXPR ( ! ::kmac::nova::LoggerTraits< TagType >::enabled ) { } \
	else if ( ::kmac::nova::Logger< TagType >::getSink() == nullptr ) { } \
	else ::kmac::nova::extras::StreamingRecordBuilder().setContext< TagType >( FILE_NAME, __func__, __LINE__ )

// wrong - hard-codes C++17
#define NOVA_LOG_STREAM( TagType ) \
	if constexpr ( enabled ) \
		if ( sink != nullptr ) \
			Builder()...
```

---

## 9. Namespaces

Namespaces are on separate lines, not nested:

```cpp
// correct - compatible with C++11 and later
namespace kmac {
namespace nova {
namespace extras {

} // namespace extras
} // namespace nova
} // namespace kmac

// wrong - C++17 or later only
namespace kmac::nova::extras {
}
```

Closing braces carry a `// namespace X` comment.  Anonymous namespaces close with `// namespace`:

```cpp
namespace {

// ...

} // namespace
```

---

## 10. Modern C++ features

**Use:** `auto`, range-`for`, `static_assert`, `constexpr`, scoped enums, `[[nodiscard]]`, `[[fallthrough]]`, `nullptr`, `= default`, `= delete`, brace initialisation, smart pointers for owning heap resources.

**Avoid:** raw owning pointers, C-style casts (use `static_cast`, `reinterpret_cast` with a comment), `std::endl` (use `'\n'`), `typedef` (use `using`).

**C-style arrays** require a `NOLINT` suppression with a justification when unavoidable (e.g. lookup tables where `std::array<std::array>` would be cumbersome):

```cpp
// NOLINT NOTE: 2D string literal lookup table; using std::array<std::array> would be cumbersome
static constexpr const char DIGITS_2[ 100 ][ 3 ] = {  // NOLINT(cppcoreguidelines-avoid-c-arrays)
	"00", "01", "02", /* ... */
};
```

**`static_cast` for narrowing conversions**, always explicit:

```cpp
r.messageSize = static_cast< std::uint32_t >( len );
_tsLen = static_cast< std::size_t >( out - _tsBuf.data() );
```

---

## 11. Safety-critical constraints

The following constraints apply to any code intended for use in DO-178C / IEC 61508 / ISO 26262 contexts.  `NOVA_BARE_METAL` implies the full set.

| Feature | Constraint |
|---|---|
| Heap allocation | forbidden in critical paths; use `MemoryPool` or fixed buffers |
| Exceptions | disabled; all public API is `noexcept` |
| RTTI | disabled |
| Dynamic dispatch | allowed for sink `process()` only |
| `std::string` | forbidden in critical paths |
| `std::vector` | forbidden in critical paths; use `platform::Array` |
| TLS | guarded by `NOVA_NO_TLS` |
| `<chrono>` | guarded by `NOVA_NO_CHRONO` |
| `<atomic>` | guarded by `NOVA_NO_ATOMIC` |
| Signal handlers | only `FdWriter` (async-signal-safe POSIX write); never `FileWriter` |

---

## 12. clang-tidy suppressions

Suppressions are a last resort - prefer refactoring.  When a suppression is genuinely required, it must include a documented justification immediately above it.

### Inline suppression (single declaration or statement)

```cpp
// NOLINT NOTE: 2D string literal lookup table; using std::array<std::array> would be cumbersome
static constexpr const char HEX_CHARS[ 16 ] = {  // NOLINT(cppcoreguidelines-avoid-c-arrays)
	'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'
};
```

### Block suppression

`NOLINTBEGIN` and `NOLINTEND` must list identical checks.  The `NOLINTEND` comment repeats the check names so it is self-documenting:

```cpp
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
char hi = HEX_CHARS[ ( byte >> 4U ) & 0x0FU ];
char lo = HEX_CHARS[ byte & 0x0FU ];
// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
```

### Complexity suppressions

State-machine `switch` blocks that legitimately exceed the cognitive complexity threshold use a trailing suppression on the function signature:

```cpp
bool CsvFormatter::formatSlow( const kmac::nova::Record& record, Buffer& buffer ) noexcept // NOLINT(readability-function-cognitive-complexity)
{
	switch ( _stage )  // NOLINT(hicpp-multiway-paths-covered)
	{
		// ...
	}
}
```

The check `cppcoreguidelines-special-member-functions` is disabled project-wide because it conflicts with the `Immovable` base class pattern.

---

## 13. Macros

### Naming

All project macros are `UPPER_SNAKE_CASE` with the `NOVA_` prefix (or `NOVA_FLARE_` for Flare-specific macros):

```cpp
NOVA_LOG( Tag )
NOVA_LOG_CONT( Tag )
NOVA_LOG_STREAM( Tag )
NOVA_LOGGER_TRAITS( TagType, tagName, enabled, timestampFn )
NOVA_IF_CONSTEXPR( condition )
NOVA_BARE_METAL
```

### Definition style

No trailing single-line comment on macro definitions.  Documentation belongs in the Doxygen block above the `#define`, not on the same line:

```cpp
// correct
/**
 * @brief Primary logging macro.  Creates a TruncatingRecordBuilder for Tag.
 */
#define NOVA_LOG( TagType ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_IF_CONSTEXPR ( ! ::kmac::nova::LoggerTraits< TagType >::enabled ) { } \
	else if ( ::kmac::nova::Logger< TagType >::getSink() == nullptr ) { } \
	else ::kmac::nova::TlsTruncBuilderWrapper< TagType >().builder()

// wrong - trailing comment on the #define line
#define NOVA_LOG( TagType ) ... // primary logging macro
```

### `NOLINT` on macros

Every macro definition suppresses `cppcoreguidelines-macro-usage` inline, since macros are necessary for `__FILE__`, `__LINE__`, and `__func__` capture:

```cpp
#define NOVA_LOG_STREAM( TagType ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
    ...
```

### Dangling-else safety

Macros that expand to conditional statements use the `NOVA_IF_CONSTEXPR (!x) {} else if (!y) {} else Z` three-clause pattern.  This ensures the entire macro expansion is syntactically a single statement, so a caller's `else` always binds to the caller's `if` and not to an inner condition inside the macro:

```cpp
// correct - three-clause chain; safe in if/else context
#define NOVA_LOG( TagType ) \
	NOVA_IF_CONSTEXPR ( ! ::kmac::nova::LoggerTraits< TagType >::enabled ) { } \
	else if ( ::kmac::nova::Logger< TagType >::getSink() == nullptr ) { } \
	else ::kmac::nova::TlsTruncBuilderWrapper< TagType >().builder()

// wrong - nested ifs; dangling-else hazard
#define NOVA_LOG( TagType ) \
	if constexpr ( ::kmac::nova::LoggerTraits< TagType >::enabled ) \
		if ( ::kmac::nova::Logger< TagType >::getSink() != nullptr ) \
			::kmac::nova::TlsTruncBuilderWrapper< TagType >().builder()
```
