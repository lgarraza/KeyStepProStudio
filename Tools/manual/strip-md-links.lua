-- Links between chapters ("03-slots.md") are dead inside one PDF: keep the text, drop the link.
function Link(el)
  if not el.target:match("^%a+://") and not el.target:match("^#") and el.target:match("%.md") then
    return el.content
  end
end
