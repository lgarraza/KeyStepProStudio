-- ```mermaid blocks become <pre class="mermaid">, which mermaid-header.html draws in the browser before printing.
local function escape(s)
  return (s:gsub("&", "&amp;"):gsub("<", "&lt;"):gsub(">", "&gt;"))
end

function CodeBlock(el)
  if el.classes:includes("mermaid") then
    return pandoc.RawBlock("html", '<pre class="mermaid">' .. escape(el.text) .. "</pre>")
  end
end
