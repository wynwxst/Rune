-- Neovim: start the Rune language server for Rune files. It gives
-- completion, hover, go to definition, references, symbols, quick fixes,
-- and diagnostics from the compiler — the borrow checker's included — and
-- the linter as you type.
--
-- vim.g.rune_lsp = 0 before this runs (in init.lua) leaves it to you or to
-- nvim-lspconfig; the Vim checker in plugin/rune.vim then runs instead.
if vim.g.loaded_rune_lsp then return end
vim.g.loaded_rune_lsp = true
if vim.g.rune_lsp == 0 or vim.g.rune_lsp == false then return end
if vim.fn.executable(vim.g.rune_rune or "rune") == 0 then return end
vim.g.rune_lsp_active = true

if vim.fn.has("nvim-0.11") == 1 then
  vim.lsp.enable("rune")
else
  vim.api.nvim_create_autocmd("FileType", {
    pattern = "rune",
    group = vim.api.nvim_create_augroup("rune_lsp", { clear = true }),
    callback = function(ev)
      local c = require("rune").config()
      local root = vim.fs.root and vim.fs.root(ev.buf, c.root_markers)
      c.root_dir = root or vim.fs.dirname(vim.api.nvim_buf_get_name(ev.buf))
      c.root_markers = nil
      vim.lsp.start(c, { bufnr = ev.buf })
    end,
  })
end

-- The server's diagnostics replace the Vim checker's.
vim.api.nvim_create_autocmd("LspAttach", {
  group = vim.api.nvim_create_augroup("rune_lsp_attach", { clear = true }),
  callback = function(ev)
    local client = vim.lsp.get_client_by_id(ev.data.client_id)
    if client and client.name == "rune" then
      vim.fn["rune#clear"](ev.buf)
    end
  end,
})
