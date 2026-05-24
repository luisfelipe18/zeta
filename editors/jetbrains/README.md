# Zeta (ζ) — Syntax Highlighting for JetBrains IDEs

Adds full syntax highlighting for `.z` files in any JetBrains IDE:
IntelliJ IDEA, CLion, PyCharm, WebStorm, GoLand, Rider, etc.

## What gets highlighted

| Element | Example |
|---|---|
| Comments | `# this is a comment` |
| Keywords | `def` `let` `mut` `struct` `impl` `return` `if` `while` `for` |
| Control flow | `if` `else` `while` `for` `in` `match` `return` |
| Built-in types | `i32` `f64` `bool` `str` `void` |
| Constants | `true` `false` |
| Built-in functions | `print(...)` `alloc(...)` `free(...)` |
| String literals | `"hello {name}"` |
| String interpolation | `{variable}` `{obj.field}` `{ptr.*}` |
| Numbers | `42` `3.14` `0xFF` |
| Operators | `+` `-` `*` `/` `==` `!=` `<=` `..` `..=` `->` |
| Ranges | `0..10` `1..=100` |
| Closures | `\|x\| x * 2` |
| Pointer deref | `ptr.*` |
| Struct names | `Vector3` `Particula` |
| Function names | `def es_primo` → `es_primo` highlighted |
| `self` | inside `impl` blocks |

---

## Installation

### Method 1 — TextMate Bundles (recommended, works in all JetBrains IDEs)

1. Open your JetBrains IDE.
2. Go to **Settings / Preferences** → **Editor** → **TextMate Bundles**.
3. Click the **+** button.
4. Navigate to and select the `zeta-lang.tmbundle` folder inside this directory.
5. Click **OK** / **Apply**.
6. Open any `.z` file — highlighting is active immediately.

> **Tip:** If you don't see "TextMate Bundles" in the settings, make sure the
> **TextMate Bundles** plugin is enabled (it is bundled with IntelliJ-based IDEs
> but may need to be activated via **Settings → Plugins → Installed**).

### Method 2 — File Type (basic, no color theme needed)

For simple keyword-only highlighting without a TextMate bundle:

1. Go to **Settings** → **Editor** → **File Types**.
2. Click **+** to add a new file type.
   - Name: `Zeta Language`
   - Description: `Z Language source file`
   - Line comment: `#`
   - Add `*.z` to the **File Name Patterns** list.
3. Under **Keywords**, add:
   ```
   def  let  mut  struct  impl  fn  type  trait  match
   if  else  while  for  in  return  and  or  not
   true  false  self  spawn  await
   i32  i64  f32  f64  bool  str  void
   print  alloc  free
   ```
4. Click **OK**.

> This method gives basic keyword coloring only. Method 1 gives full highlighting
> including strings, interpolation, operators, and type annotations.

---

## Color theme compatibility

The grammar uses standard TextMate scope names, so it works with any color theme:

| Scope | Typical color |
|---|---|
| `keyword.control.z` | orange / magenta |
| `keyword.declaration.z` | purple / blue |
| `entity.name.function.z` | yellow / gold |
| `entity.name.type.z` | cyan / teal |
| `storage.type.z` | cyan |
| `string.quoted.double.z` | green |
| `meta.interpolation.z` | bright / highlighted within string |
| `constant.numeric.*.z` | blue / magenta |
| `constant.language.z` | orange |
| `comment.line.number-sign.z` | gray (italic) |
| `keyword.operator.*.z` | white / light |
| `support.function.builtin.z` | cyan / teal |

Recommended themes: **Dracula**, **One Dark**, **Monokai Pro**, **Nord**.

---

## VS Code / Sublime Text

The same `z.tmLanguage` file works in VS Code and Sublime Text:

**VS Code:**
Place `z.tmLanguage` inside a VS Code extension's `syntaxes/` folder, or use
the [TextMate Language](https://marketplace.visualstudio.com/items?itemName=Togusa09.tmlanguage)
extension to load it directly.

**Sublime Text:**
Copy `z.tmLanguage` to:
- macOS: `~/Library/Application Support/Sublime Text/Packages/User/`
- Linux: `~/.config/sublime-text/Packages/User/`
- Windows: `%APPDATA%\Sublime Text\Packages\User\`
