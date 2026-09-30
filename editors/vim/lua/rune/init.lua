-- Rune for Neovim: the language server's configuration, and how the Vim
-- checker's findings are shown when the server is not running.
local M = {}

local ns = vim.api.nvim_create_namespace("rune")
local severity = { E = 1, W = 2, I = 3, N = 4 }

--- Shows the checker's location-list `items` for `buf` as diagnostics, so
--- they are underlined, signed and listed like any server's.
function M.show(buf, items)
  if not vim.api.nvim_buf_is_valid(buf) then return end
  local out = {}
  for _, it in ipairs(items) do
    table.insert(out, {
      lnum = it.lnum - 1,
      col = it.col - 1,
      end_lnum = it.end_lnum - 1,
      end_col = it.end_col - 1,
      severity = severity[it.type] or 1,
      message = it.text,
      source = it.source,
    })
  end
  vim.diagnostic.set(ns, buf, out)
end

--- The settings `rune lsp` reads; `opts` overrides any of them.
function M.settings(opts)
  local g = vim.g
  return vim.tbl_deep_extend("force", {
    checkOnSave = true,
    check = {
      onChange = g.rune_check_on_change ~= 0 and g.rune_check_on_change ~= false,
      delay = g.rune_check_delay or 400,
      memory = g.rune_memory or "arc",
    },
    lint = { enable = g.rune_lint ~= 0 and g.rune_lint ~= false },
  }, opts or {})
end

--- A configuration for `vim.lsp.config`, `vim.lsp.start` or nvim-lspconfig.
function M.config(opts)
  local settings = M.settings(opts)
  return {
    name = "rune",
    cmd = { vim.g.rune_rune or "rune", "lsp" },
    filetypes = { "rune" },
    root_markers = { "Rune.toml", ".git" },
    init_options = settings,
    settings = { rune = settings },
  }
end

return M
