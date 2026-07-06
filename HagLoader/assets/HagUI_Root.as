// HagUI root boot + Welcome screen — overwrites scripts/frame_1/DoAction.as in the
// cloned Credits CLIK shell. Keeps _root drawable, wires close, and renders a
// black+gold "gentlemen's club" Welcome built entirely in AS2 (gradients, embedded
// Skyrim fonts, rounded gold-hairline panels) — the Manga List design language.
//
// Tokens (from Manga List theme.css):
//   bg-0 #0A0A0C  bg-1 #121013  panel #1A1712  panel-2 #231E16  warm #241D0F
//   accent #E0B34A  accent-dim #B8862F  text #ECE6DA  text-dim #9C9486  text-faint #6B6456
//   radius 10/14  border gold@14%  border-strong gold@42%

function onCodeObjectInit()
{
   Menu_mc.onCodeObjectInit();
}
function ReleaseCodeObject()
{
   delete CodeObj;
}

// ---- rounded-rect path helpers (fill/line begun by the caller) ----
function rrPath(mc, x, y, w, h, r)
{
   mc.moveTo(x + r, y);
   mc.lineTo(x + w - r, y);
   mc.curveTo(x + w, y, x + w, y + r);
   mc.lineTo(x + w, y + h - r);
   mc.curveTo(x + w, y + h, x + w - r, y + h);
   mc.lineTo(x + r, y + h);
   mc.curveTo(x, y + h, x, y + h - r);
   mc.lineTo(x, y + r);
   mc.curveTo(x, y, x + r, y);
}
function rect(mc, x, y, w, h)
{
   mc.moveTo(x, y); mc.lineTo(x + w, y); mc.lineTo(x + w, y + h); mc.lineTo(x, y + h); mc.lineTo(x, y);
}
// rounded only on the left side (for the accent rail)
function rrLeft(mc, x, y, w, h, r)
{
   mc.moveTo(x + r, y);
   mc.lineTo(x + w, y);
   mc.lineTo(x + w, y + h);
   mc.lineTo(x + r, y + h);
   mc.curveTo(x, y + h, x, y + h - r);
   mc.lineTo(x, y + r);
   mc.curveTo(x, y, x + r, y);
}
// FFDec's AS2 compiler can't parse inline object literals, so build the gradient "box" matrix here.
function boxM(x, y, w, h, r)
{
   var m = new Object();
   m.matrixType = "box"; m.x = x; m.y = y; m.w = w; m.h = h; m.r = r;
   return m;
}
function mkText(parent, name, depth, x, y, w, h, html)
{
   parent.createTextField(name, depth, x, y, w, h);
   var t = parent[name];
   t.selectable = false;
   t.embedFonts = true;
   t.antiAliasType = "advanced";
   t.multiline = true;
   t.wordWrap = true;
   t.html = true;
   t.htmlText = html;
   return t;
}

// ---- reusable gold button: gradient bg + border, brightens on hover, fires a GameDelegate
// callback on its own click (direct hit-test, not the backdrop rectangle). ----
function paintButton(b, hover, pressed)
{
   b.clear();
   var a1 = 18;
   var a2 = 6;
   var ba = 42;
   if (hover) { a1 = 34; a2 = 14; ba = 78; }
   if (pressed) { a1 = 46; a2 = 22; ba = 96; }
   b.lineStyle(1, 0xE0B34A, ba, true, "none", "round", "round");
   b.beginGradientFill("linear", [0xE0B34A, 0xE0B34A], [a1, a2], [0, 255], _root.boxM(b._bx, b._by, b._bw, b._bh, Math.PI / 2));
   _root.rrPath(b, b._bx, b._by, b._bw, b._bh, 7);
   b.endFill();
}
function makeButton(parent, name, depth, bx, by, bw, bh, html, cb)
{
   // hover glow sits BEHIND as its own clip so it never enlarges the button's hit area
   var g = parent.createEmptyMovieClip(name + "_glow", depth);
   g.beginFill(0xE0B34A, 16);
   _root.rrPath(g, bx - 5, by - 5, bw + 10, bh + 10, 11);
   g.endFill();
   g._visible = false;
   var b = parent.createEmptyMovieClip(name, depth + 1);
   b._bx = bx; b._by = by; b._bw = bw; b._bh = bh; b._cb = cb; b._glow = g;
   _root.paintButton(b, false, false);
   var lbl = _root.mkText(b, "lbl", 1, bx, by, bw, 30, html);
   // vertical-center on the REAL text height (minus Flash's 2px top gutter) instead of a guess
   lbl._y = by + Math.round((bh - lbl.textHeight) / 2) - 2;
   b.onRollOver = function() { _root.paintButton(this, true, false); this._glow._visible = true; };
   b.onRollOut = function() { this._x = 0; this._y = 0; _root.paintButton(this, false, false); this._glow._visible = false; };
   b.onPress = function() { this._x = 1; this._y = 1; _root.paintButton(this, true, true); this._glow._visible = true; };
   b.onReleaseOutside = function() { this._x = 0; this._y = 0; _root.paintButton(this, false, false); this._glow._visible = false; };
   b.onRelease = function() { this._x = 0; this._y = 0; _root.paintButton(this, true, false); gfx.io.GameDelegate.call(this._cb, []); };
   return b;
}

// ---- navigation tabs. _root.HAG_TABS holds the labels (addTab appends one); the active tab
// is gold + underlined, inactive tabs are dim and brighten on hover; showPage builds each page.
// New tabs: addTab("LABEL") + a branch in showPage() for its content builder. ----
function tabHtml(label, act)
{
   var col = "#9C9486";
   if (act) { col = "#E0B34A"; }
   return "<font face='$EverywhereBoldFont' size='15' color='" + col + "'>" + label + "</font>";
}
function buildNav(card, nx, ny, nw)
{
   var nav = card.createEmptyMovieClip("nav", 18);
   nav.lineStyle(1, 0xE0B34A, 16);                         // baseline hairline under the row
   nav.moveTo(nx, ny + 34); nav.lineTo(nx + nw, ny + 34);
   var pad = 16;     // equal free space on EACH side of the label, inside the highlight
   var gap = 12;     // space between tab cells
   var cx = nx;
   var i = 0;
   while (i < _root.HAG_TABS.length)
   {
      var act = (i == _root.hagActive);
      var t = nav.createEmptyMovieClip("t" + i, 10 + i);
      t._idx = i;
      var lbl = _root.mkText(t, "lbl", 2, cx, ny + 6, 300, 24, _root.tabHtml(_root.HAG_TABS[i], act));
      var tw = lbl.textWidth;
      if (!(tw > 10)) { tw = _root.HAG_TABS[i].length * 11 + 4; }
      var cellW = tw + pad * 2;          // the highlight is wider than the text by 2*pad
      lbl._x = cx + pad - 2;             // center the label inside the cell (-2 = Flash text gutter)
      t.beginFill(0xFFFFFF, 0); _root.rect(t, cx, ny, cellW, 35); t.endFill();              // hit area = the cell
      if (act) { t.lineStyle(2, 0xE0B34A, 100); t.moveTo(cx, ny + 34); t.lineTo(cx + cellW, ny + 34); }  // underline spans the cell
      t.onRelease = function() { _root.selectTab(this._idx); };
      t.onRollOver = function() { _root.tabHover(this._idx, true); };
      t.onRollOut = function() { _root.tabHover(this._idx, false); };
      cx = cx + cellW + gap;
      i = i + 1;
   }
}
function tabHover(idx, over)
{
   if (idx == _root.hagActive) { return; }
   var col = "#9C9486";
   if (over) { col = "#ECE6DA"; }
   _root.HagWelcome.card.nav["t" + idx].lbl.htmlText =
      "<font face='$EverywhereBoldFont' size='15' color='" + col + "'>" + _root.HAG_TABS[idx] + "</font>";
}
function addTab(label) { _root.HAG_TABS.push(label); }
function selectTab(idx)
{
   if (idx == _root.hagActive) { return; }
   _root.hagActive = idx;
   var card = _root.HagWelcome.card;
   card.nav.removeMovieClip();
   _root.buildNav(card, card._nx, card._ny, card._nw);
   _root.showPage(idx);
}
function showPage(idx)
{
   var card = _root.HagWelcome.card;
   card.content.removeMovieClip();
   var c = card.createEmptyMovieClip("content", 22);
   // tab 0 is always the Welcome page; tabs 1..N are registered option pages (idx-1 in HAG_PAGES).
   if (idx == 0) { _root.buildWelcomePage(c, card._cx, card._cyTop, card._cw2); }
   else { _root.buildOptionPage(c, card._cx, card._cyTop, card._cw2, idx - 1); }
   _root.showCloseButton(card, idx == 0);   // CLOSE button is exclusive to the Welcome tab
}
function buildWelcomePage(c, x, y, w)
{
   _root.mkText(c, "lbl", 1, x, y, w, 22,
      "<font face='$EverywhereBoldFont' size='13' color='#6B6456'>W&nbsp;E&nbsp;L&nbsp;C&nbsp;O&nbsp;M&nbsp;E&nbsp;&nbsp;T&nbsp;O</font>");
   _root.mkText(c, "mark", 2, x - 2, y + 14, w, 92,
      "<font face='$EverywhereBoldFont' size='64' color='#ECE6DA'>Hag<font color='#E0B34A'>UI</font></font>");
   var dv = c.createEmptyMovieClip("dv", 3);
   dv.beginGradientFill("linear", [0xE0B34A, 0xE0B34A], [60, 0], [0, 255], _root.boxM(x, 0, 250, 2, 0));
   _root.rect(dv, x, y + 112, 250, 2); dv.endFill();
   _root.mkText(c, "tag", 4, x, y + 132, w, 70,
      "<font face='$EverywhereFont' size='18' color='#9C9486'>Your private control room for every Hagryph mod &#8212;<br>configuration, tools, and more, gathered in one place.</font>");
}

// The CLOSE button + "or press ESC" hint are EXCLUSIVE to the Welcome tab. On any other tab we
// remove them (option pages close via ESC or an outside-click, handled in HagUI_Menu.as). Called
// by showPage() on every tab change; geometry comes from card._btnX/_btnY/_btnW/_btnH.
function showCloseButton(card, on)
{
   card.closeBtn.removeMovieClip();
   card.closeBtn_glow.removeMovieClip();
   card.hint.removeTextField();   // hint is a TextField (mkText), NOT a MovieClip -> removeMovieClip no-ops
   if (on)
   {
      _root.makeButton(card, "closeBtn", 28, card._btnX, card._btnY, card._btnW, card._btnH,
         "<p align='center'><font face='$EverywhereBoldFont' size='15' color='#E0B34A'>CLOSE</font></p>", "CloseHagUI");
      _root.mkText(card, "hint", 31, card._btnX + card._btnW + 18, card._btnY + 11, 230, 22,
         "<font face='$EverywhereFont' size='14' color='#6B6456'>or press&nbsp;&nbsp;ESC</font>");
   }
}

// ---- option pages (registered by mods through the HagUI host API and pushed into the movie by C++
// as a flat _root.hagPage<i>_opt<j>_* model; HagBuildPages() reads it into _root.HAG_PAGES). ----

// gold checkbox box: hairline square, faint fill (brighter when checked), gold check glyph when on.
function paintCheckbox(box, checked, hover, enabled)
{
   box.clear();
   var col = 0xE0B34A;
   if (!enabled) { col = 0x6B6456; }         // dim gold when greyed out (disabled)
   var ba = 42;
   if (hover && enabled) { ba = 82; }
   box.lineStyle(1, col, ba, true, "none", "round", "round");
   var fa = 6;
   if (checked) { fa = 22; }
   box.beginFill(col, fa);
   _root.rrPath(box, 0, 0, 22, 22, 5);
   box.endFill();
   if (checked)
   {
      box.lineStyle(2, col, 100, true, "none", "round", "round");
      box.moveTo(5, 11); box.lineTo(9, 16); box.lineTo(17, 6);
   }
}
// a full-width clickable row: checkbox + label. Click flips the box and reports the new value to
// C++ via the native _root.hagSetOption(pageIdx, optIdx, value) function (all Number args).
function makeCheckbox(parent, name, depth, x, y, w, op)
{
   var en = (op.enabled != 0);
   var row = parent.createEmptyMovieClip(name, depth);
   row._x = x; row._y = y;
   row._op = op;
   row._checked = (op.value != 0);
   row.beginFill(0xFFFFFF, 0); _root.rect(row, 0, 0, w, 30); row.endFill();   // invisible hit area
   var box = row.createEmptyMovieClip("box", 2);
   box._y = 4;
   _root.paintCheckbox(box, row._checked, false, en);
   var lblCol = "#ECE6DA";
   if (!en) { lblCol = "#6B6456"; }          // dim the label too when disabled
   _root.mkText(row, "lbl", 3, 36, 3, w - 36, 26,
      "<font face='$EverywhereFont' size='17' color='" + lblCol + "'>" + op.label + "</font>");
   // per-option note (e.g. "applies after restart") — right-aligned on the row, dim gold italic
   if (op.note != undefined && op.note != "" && op.note != "undefined")
   {
      _root.mkText(row, "note", 4, w - 236, 6, 232, 18,
         "<p align='right'><font face='$EverywhereFont' size='12' color='#B8862F'><i>" + op.note + "</i></font></p>");
   }
   if (en)
   {
      row.onRollOver = function() { _root.paintCheckbox(this.box, this._checked, true, true); };
      row.onRollOut  = function() { _root.paintCheckbox(this.box, this._checked, false, true); };
      row.onReleaseOutside = function() { _root.paintCheckbox(this.box, this._checked, false, true); };
      row.onRelease  = function()
      {
         this._checked = !this._checked;
         _root.paintCheckbox(this.box, this._checked, true, true);
         if (_root.hagSetOption) { _root.hagSetOption(this._op.pageIdx, this._op.optIdx, this._checked ? 1 : 0); }
      };
   }
   // disabled rows get NO handlers -> not clickable (greyed out)
   return row;
}
// ---- Action button widget (type 4): explicit command button for one-shot mod actions. ----
function paintActionButton(b, hover, enabled, pressed)
{
   b.clear();
   var a1 = 20;
   var a2 = 7;
   var ba = 44;
   var tc = 0xE0B34A;
   if (!enabled) { a1 = 6; a2 = 3; ba = 18; tc = 0x6B6456; }
   else if (hover) { a1 = 38; a2 = 16; ba = 82; }
   if (enabled && pressed) { a1 = 50; a2 = 26; ba = 100; }
   b.lineStyle(1, tc, ba, true, "none", "round", "round");
   b.beginGradientFill("linear", [0xE0B34A, 0xB8862F], [a1, a2], [0, 255],
      _root.boxM(0, 0, b._bw, b._bh, Math.PI / 2));
   _root.rrPath(b, 0, 0, b._bw, b._bh, 7);
   b.endFill();
}
function paintActionIndicator(ind, hover, enabled, pressed)
{
   ind.clear();
   var col = 0xE0B34A;
   var border = 44;
   var fill = 6;
   if (!enabled) { col = 0x6B6456; border = 18; fill = 3; }
   else if (hover) { border = 86; fill = 18; }
   if (enabled && pressed) { border = 100; fill = 28; }
   ind.lineStyle(1, col, border, true, "none", "round", "round");
   ind.beginFill(col, fill);
   _root.rrPath(ind, 0, 0, 22, 22, 5);
   ind.endFill();
   ind.lineStyle(2, col, enabled ? 100 : 45, true, "none", "round", "round");
   ind.moveTo(8, 6);
   ind.lineTo(14, 11);
   ind.lineTo(8, 16);
}
function makeActionButton(parent, name, depth, x, y, w, op)
{
   var en = (op.enabled != 0);
   var row = parent.createEmptyMovieClip(name, depth);
   row._x = x; row._y = y;
   row._op = op;

   var bw = op.label.length * 10 + 70;
   if (bw < 180) { bw = 180; }
   if (bw > w) { bw = w; }
   row.beginFill(0xFFFFFF, 0); _root.rect(row, 0, 0, bw, 32); row.endFill();

   var ind = row.createEmptyMovieClip("ind", 2);
   ind._x = 0; ind._y = 5;
   _root.paintActionIndicator(ind, false, en, false);
   var col = en ? "#E0B34A" : "#6B6456";
   var t = _root.mkText(row, "text", 3, 34, 3, bw - 34, 24,
      "<font face='$EverywhereBoldFont' size='15' color='" + col + "'>" + op.label + "</font>");
   row._labelHtml = "<font face='$EverywhereBoldFont' size='15' color='#E0B34A'>" + op.label + "</font>";
   row._labelHoverHtml = "<font face='$EverywhereBoldFont' size='15' color='#ECE6DA'>" + op.label + "</font>";
   row._labelDisabledHtml = "<font face='$EverywhereBoldFont' size='15' color='#6B6456'>" + op.label + "</font>";

   if (op.note != undefined && op.note != "" && op.note != "undefined")
   {
      _root.mkText(row, "note", 4, bw + 14, 8, w - bw - 14, 20,
         "<font face='$EverywhereFont' size='12' color='#B8862F'><i>" + op.note + "</i></font>");
   }
   if (en)
   {
      row.onRollOver = function() { _root.paintActionIndicator(this.ind, true, true, false); this.text.htmlText = this._labelHoverHtml; };
      row.onRollOut = function() { this.ind._x = 0; this.text._x = 34; _root.paintActionIndicator(this.ind, false, true, false); this.text.htmlText = this._labelHtml; };
      row.onPress = function() { this.ind._x = 1; this.text._x = 35; _root.paintActionIndicator(this.ind, true, true, true); this.text.htmlText = this._labelHoverHtml; };
      row.onReleaseOutside = function() { this.ind._x = 0; this.text._x = 34; _root.paintActionIndicator(this.ind, false, true, false); this.text.htmlText = this._labelHtml; };
      row.onRelease = function()
      {
         this.ind._x = 0; this.text._x = 34; _root.paintActionIndicator(this.ind, true, true, false); this.text.htmlText = this._labelHoverHtml;
         if (_root.hagSetOption) { _root.hagSetOption(this._op.pageIdx, this._op.optIdx, 1); }
      };
   }
   return row;
}
// ---- Hotkey widget (type 7): captures Win32 virtual-key codes for runtime keybinds. ----
function keyName(code)
{
   var c = Number(code);
   if (c >= 65 && c <= 90) { return String.fromCharCode(c); }
   if (c >= 48 && c <= 57) { return String.fromCharCode(c); }
   if (c >= 112 && c <= 123) { return "F" + (c - 111); }
   if (c == 8) { return "BACKSPACE"; }
   if (c == 9) { return "TAB"; }
   if (c == 13) { return "ENTER"; }
   if (c == 16) { return "SHIFT"; }
   if (c == 17) { return "CTRL"; }
   if (c == 18) { return "ALT"; }
   if (c == 20) { return "CAPS"; }
   if (c == 27) { return "ESC"; }
   if (c == 32) { return "SPACE"; }
   if (c == 33) { return "PAGE UP"; }
   if (c == 34) { return "PAGE DOWN"; }
   if (c == 35) { return "END"; }
   if (c == 36) { return "HOME"; }
   if (c == 37) { return "LEFT"; }
   if (c == 38) { return "UP"; }
   if (c == 39) { return "RIGHT"; }
   if (c == 40) { return "DOWN"; }
   if (c == 45) { return "INSERT"; }
   if (c == 46) { return "DELETE"; }
   if (c == 186) { return ";"; }
   if (c == 187) { return "="; }
   if (c == 188) { return ","; }
   if (c == 189) { return "-"; }
   if (c == 190) { return "."; }
   if (c == 191) { return "/"; }
   if (c == 192) { return "`"; }
   if (c == 219) { return "["; }
   if (c == 220) { return "\\"; }
   if (c == 221) { return "]"; }
   if (c == 222) { return "'"; }
   if (c > 0) { return "VK " + c; }
   return "UNBOUND";
}
function flashKeyToVk(code)
{
   var c = Number(code);
   if (c >= 1 && c <= 255) { return c; }
   return -1;
}
function hotkeyHtml(label, active, enabled)
{
   var col = "#E0B34A";
   if (!enabled) { col = "#6B6456"; }
   var text = _root.keyName(label);
   if (active) { text = "PRESS A KEY"; }
   return "<p align='center'><font face='$EverywhereBoldFont' size='14' color='" + col + "'>" + text + "</font></p>";
}
function finishHotkeyCapture(keyCode)
{
   var b = _root.hagHotkeyCapture;
   if (!b) { return; }
   _root.hagHotkeyCapture = null;
   if (keyCode > 0)
   {
      b._op.value = keyCode;
      b.text.htmlText = _root.hotkeyHtml(keyCode, false, true);
      if (_root.hagSetOption) { _root.hagSetOption(b._op.pageIdx, b._op.optIdx, keyCode); }
   }
   else
   {
      b.text.htmlText = _root.hotkeyHtml(b._op.value, false, true);
   }
}
function ensureHotkeyListener()
{
   if (_root.hagHotkeyListener) { return; }
   var lis = new Object();
   lis.onKeyDown = function()
   {
      if (!_root.hagHotkeyCapture) { return; }
      var vk = _root.flashKeyToVk(Key.getCode());
      if (vk == 27) { _root.finishHotkeyCapture(-1); return; }
      if (vk > 0) { _root.finishHotkeyCapture(vk); }
   };
   Key.addListener(lis);
   _root.hagHotkeyListener = lis;
}
function makeHotkey(parent, name, depth, x, y, w, op)
{
   _root.ensureHotkeyListener();
   var en = (op.enabled != 0);
   var row = parent.createEmptyMovieClip(name, depth);
   row._x = x; row._y = y;
   row._op = op;
   row.beginFill(0xFFFFFF, 0); _root.rect(row, 0, 0, w, 38); row.endFill();

   var lblCol = en ? "#ECE6DA" : "#6B6456";
   var lbl = _root.mkText(row, "lbl", 1, 0, 5, w - 154, 24,
      "<font face='$EverywhereFont' size='17' color='" + lblCol + "'>" + op.label + "</font>");

   var bw = 84;
   var bh = 30;
   var by = 2;
   var gap = 24;
   var lw = lbl.textWidth;
   if (!(lw > 10)) { lw = op.label.length * 8; }
   var bx = Math.round(lw + gap);
   if (bx > w - bw) { bx = w - bw; }
   var b = row.createEmptyMovieClip("btn", 2);
   b._x = bx; b._y = by; b._baseX = bx; b._baseY = by; b._bw = bw; b._bh = bh; b._op = op;
   _root.paintActionButton(b, false, en, false);
   var txt = _root.mkText(b, "text", 1, 0, 0, bw, 24, _root.hotkeyHtml(op.value, false, en));
   txt._y = Math.round((bh - txt.textHeight) / 2) - 2;

   if (op.note != undefined && op.note != "" && op.note != "undefined")
   {
      _root.mkText(row, "note", 3, 0, 27, w - 190, 18,
         "<font face='$EverywhereFont' size='12' color='#B8862F'><i>" + op.note + "</i></font>");
   }
   if (en)
   {
      b.onRollOver = function() { _root.paintActionButton(this, true, true, false); };
      b.onRollOut = function() { this._x = this._baseX; this._y = this._baseY; _root.paintActionButton(this, false, true, false); };
      b.onPress = function() { this._x = this._baseX + 1; this._y = this._baseY + 1; _root.paintActionButton(this, true, true, true); };
      b.onReleaseOutside = function() { this._x = this._baseX; this._y = this._baseY; _root.paintActionButton(this, false, true, false); };
      b.onRelease = function()
      {
         this._x = this._baseX; this._y = this._baseY; _root.paintActionButton(this, true, true, false);
         _root.hagHotkeyCapture = this;
         this.text.htmlText = _root.hotkeyHtml(this._op.value, true, true);
      };
   }
   return row;
}
// ---- ProgressBar widget (type 5): read-only, live. C++ pushes _fill (0..1) + _bartext each tick and
// calls HagUpdateBars(); we resize the fill pill in place (registered in _root.HAG_BARS). ----
function paintBar(bar, frac, w, h, color)
{
   bar.clear();
   bar.lineStyle(1, color, 26, true, "none", "round", "round");   // track
   bar.beginFill(0x231E16, 90);
   _root.rrPath(bar, 0, 0, w, h, h / 2);
   bar.endFill();
   var fw = Math.round(w * frac);                                 // fill
   if (fw > 0 && fw < h) { fw = h; }        // keep a rounded pill visible for tiny nonzero values
   if (fw > w) { fw = w; }
   if (fw > 0)
   {
      bar.beginFill(color, 92);
      _root.rrPath(bar, 0, 0, fw, h, h / 2);
      bar.endFill();
   }
}
function buildProgressBar(parent, name, depth, x, y, w, op)
{
   var color = Number(op.color);
   if (!(color >= 0)) { color = 0xE0B34A; }
   var row = parent.createEmptyMovieClip(name, depth);
   row._x = x; row._y = y;
   _root.mkText(row, "lbl", 1, 0, 0, w - 130, 20,
      "<font face='$EverywhereBoldFont' size='14' color='#ECE6DA'>" + op.label + "</font>");
   var bt = (op.bartext == undefined || op.bartext == "undefined") ? "" : op.bartext;
   var val = _root.mkText(row, "val", 2, w - 130, 1, 130, 20,
      "<p align='right'><font face='$EverywhereFont' size='13' color='#9C9486'>" + bt + "</font></p>");
   var barH = 14;
   var bar = row.createEmptyMovieClip("bar", 3);
   bar._y = 24;
   var frac = Number(op.fill);
   if (!(frac >= 0)) { frac = 0; }
   if (frac > 1) { frac = 1; }
   _root.paintBar(bar, frac, w, barH, color);
   if (!_root.HAG_BARS) { _root.HAG_BARS = new Array(); }
   var rec = new Object();
   rec.bar = bar; rec.val = val; rec.i = op.pageIdx; rec.j = op.optIdx; rec.w = w; rec.h = barH; rec.color = color;
   _root.HAG_BARS.push(rec);
   return row;
}
// ---- Counter widget (type 8): read-only live text, rendered as a compact value capsule. ----
function paintCounterPill(pill, w, h)
{
   pill.clear();
   pill.lineStyle(1, 0xE0B34A, 38, true, "none", "round", "round");
   pill.beginGradientFill("linear", [0xE0B34A, 0xB8862F], [17, 6], [0, 255],
      _root.boxM(0, 0, w, h, Math.PI / 2));
   _root.rrPath(pill, 0, 0, w, h, 6);
   pill.endFill();
}
function buildCounter(parent, name, depth, x, y, w, op)
{
   var row = parent.createEmptyMovieClip(name, depth);
   row._x = x; row._y = y;
   var lbl = _root.mkText(row, "lbl", 1, 0, 5, w - 150, 24,
      "<font face='$EverywhereFont' size='17' color='#ECE6DA'>" + op.label + "</font>");
   var vw = 126;
   var vh = 30;
   var vy = 2;
   var gap = 24;
   var lw = lbl.textWidth;
   if (!(lw > 10)) { lw = op.label.length * 8; }
   var vx = Math.round(lw + gap);
   if (vx > w - vw) { vx = w - vw; }
   var pill = row.createEmptyMovieClip("pill", 2);
   pill._x = vx; pill._y = vy;
   _root.paintCounterPill(pill, vw, vh);
   var bt = (op.bartext == undefined || op.bartext == "undefined") ? "" : op.bartext;
   var val = _root.mkText(pill, "val", 1, 0, 0, vw, 22,
      "<p align='center'><font face='$EverywhereBoldFont' size='13' color='#E0B34A'>" + bt + "</font></p>");
   val._y = Math.round((vh - val.textHeight) / 2) - 2;
   if (!_root.HAG_COUNTERS) { _root.HAG_COUNTERS = new Array(); }
   var crec = new Object();
   crec.val = val; crec.i = op.pageIdx; crec.j = op.optIdx;
   _root.HAG_COUNTERS.push(crec);
   return row;
}
// Called by C++ (OptionRender::UpdateLive) every menu tick — cheap: just resizes the fill + text.
function HagUpdateBars()
{
   if (!_root.HAG_BARS) { _root.HAG_BARS = new Array(); }
   var k = 0;
   while (k < _root.HAG_BARS.length)
   {
      var rec = _root.HAG_BARS[k];
      var frac = Number(_root["hagPage" + rec.i + "_opt" + rec.j + "_fill"]);
      if (!(frac >= 0)) { frac = 0; }
      if (frac > 1) { frac = 1; }
      _root.paintBar(rec.bar, frac, rec.w, rec.h, rec.color);
      var t = _root["hagPage" + rec.i + "_opt" + rec.j + "_bartext"];
      if (t != undefined)
      {
         rec.val.htmlText = "<p align='right'><font face='$EverywhereFont' size='13' color='#9C9486'>" + t + "</font></p>";
      }
      k = k + 1;
   }
   if (!_root.HAG_COUNTERS) { return; }
   k = 0;
   while (k < _root.HAG_COUNTERS.length)
   {
      var crec = _root.HAG_COUNTERS[k];
      var ct = _root["hagPage" + crec.i + "_opt" + crec.j + "_bartext"];
      if (ct != undefined)
      {
         crec.val.htmlText = "<p align='center'><font face='$EverywhereBoldFont' size='13' color='#E0B34A'>" + ct + "</font></p>";
      }
      k = k + 1;
   }
}
// ---- Model3D widget (type 6): a framed panel that will host the img://hagCharModel virtual-image
// texture HagUI renders the player into (Route A). Until the render-to-texture is wired, it shows the
// framed placeholder; loadMovie("img://hagCharModel") is enabled once the texture is registered. ----
function buildModel3D(parent, name, depth, x, y, w, h, op)
{
   var m = parent.createEmptyMovieClip(name, depth);
   m.lineStyle(1, 0xE0B34A, 30, true, "none", "round", "round");
   m.beginFill(0x0A0A0C, 92);
   _root.rrPath(m, x, y, w, h, 10);
   m.endFill();
   if (_root.hagModelReady)
   {
      // load our native img:// texture into a child clip, masked to the panel + scaled to fit
      var cont = m.createEmptyMovieClip("cont", 2);
      var img = cont.createEmptyMovieClip("img", 1);
      img._x = x; img._y = y;
      var mk = m.createEmptyMovieClip("imask", 3);
      mk.beginFill(0xFFFFFF, 100); _root.rrPath(mk, x, y, w, h, 10); mk.endFill();
      cont.setMask(mk);
      var mcl = new MovieClipLoader();
      var lis = new Object();
      lis.px = x; lis.py = y; lis.pw = w; lis.ph = h;
      lis.onLoadInit = function(t)
      {
         var iw = t._width;
         var ih = t._height;
         if (iw > 0 && ih > 0)
         {
            var s = Math.min(this.pw / iw, this.ph / ih);
            t._xscale = s * 100;
            t._yscale = s * 100;
            t._x = this.px + (this.pw - iw * s) / 2;
            t._y = this.py + (this.ph - ih * s) / 2;
         }
      };
      mcl.addListener(lis);
      _root.hagMcl = mcl;   // keep the loader alive past this scope
      mcl.loadClip("img://hagCharModel", img);
   }
   else
   {
      _root.mkText(m, "hint", 4, x, y + h / 2 - 14, w, 28,
         "<p align='center'><font face='$EverywhereFont' size='14' color='#6B6456'>" + op.label + "</font></p>");
   }
   return m;
}
function gridKey(col, row)
{
   return "c" + col + "r" + row;
}
function gridRenderable(op)
{
   return op.type == 0 || op.type == 4 || op.type == 5 || op.type == 6 || op.type == 7 || op.type == 8;
}
function gridControlRows(op)
{
   if (op.type == 5) { return 2; }   // ProgressBar gets a full readout row plus bar row.
   if (op.type == 6) { return 5; }   // Model3D has a predefined tall cell.
   return 1;
}
function gridControlHeight(op, rowH)
{
   if (op.type == 5) { return 52; }
   if (op.type == 6) { return rowH * 5 - 12; }
   return 40;
}
function gridCanUse(used, col, row, span)
{
   if (col < 0 || col > 1 || row < 0) { return false; }
   var i = 0;
   while (i < span)
   {
      if (used[_root.gridKey(col, row + i)]) { return false; }
      i = i + 1;
   }
   return true;
}
function gridReserve(used, col, row, span)
{
   var i = 0;
   while (i < span)
   {
      used[_root.gridKey(col, row + i)] = true;
      i = i + 1;
   }
}
function gridNextSlot(used, span)
{
   var n = 0;
   while (n < 200)
   {
      var row = Math.floor(n / 2);
      var col = n - row * 2;   // row-major: left, right, next row.
      if (_root.gridCanUse(used, col, row, span))
      {
         var slot = new Object();
         slot.col = col;
         slot.row = row;
         return slot;
      }
      n = n + 1;
   }
   var fallback = new Object();
   fallback.col = 0;
   fallback.row = 0;
   return fallback;
}
function paintGridShell(parent, x, y, w, h, colW, gap)
{
   var sh = parent.createEmptyMovieClip("gridShell", 4);
   sh.lineStyle(1, 0xE0B34A, 24);
   var mid = x + colW + Math.round(gap / 2);
   sh.moveTo(mid, y + 6);
   sh.lineTo(mid, y + h - 6);
}
function gridPlaceControl(c, i, depth, op, x, y, w, h)
{
   if (op.type == 0)
   {
      _root.makeCheckbox(c, "row" + i, depth, x, y, w, op);
   }
   else if (op.type == 5)
   {
      _root.buildProgressBar(c, "bar" + i, depth, x, y, w, op);
   }
   else if (op.type == 4)
   {
      _root.makeActionButton(c, "btn" + i, depth, x, y, w, op);
   }
   else if (op.type == 7)
   {
      _root.makeHotkey(c, "hotkey" + i, depth, x, y, w, op);
   }
   else if (op.type == 8)
   {
      _root.buildCounter(c, "counter" + i, depth, x, y, w, op);
   }
   else if (op.type == 6)
   {
      _root.buildModel3D(c, "model" + i, depth, x, y, w, h, op);
   }
}
function buildOptionPage(c, x, y, w, pageIdx)
{
   var pg = _root.HAG_PAGES[pageIdx];
   if (!pg) { return; }
   _root.HAG_BARS = new Array();   // reset the live-bar registry for this page render
   _root.HAG_COUNTERS = new Array();
   _root.mkText(c, "hd", 1, x, y, w, 26,
      "<font face='$EverywhereBoldFont' size='21' color='#ECE6DA'>" + pg.title + "</font>");
   var topY = y + 44;
   var rowH = 48;
   var doublePage = (pg.doublePage == 1);
   var depth = 10;
   var i = 0;

   if (!doublePage)
   {
      var flowY = topY + 8;
      while (i < pg.opts.length)
      {
         var flowOp = pg.opts[i];
         if (_root.gridRenderable(flowOp))
         {
            var flowRows = _root.gridControlRows(flowOp);
            var flowH = _root.gridControlHeight(flowOp, rowH);
            _root.gridPlaceControl(c, i, depth, flowOp, x, flowY, w, flowH);
            flowY = flowY + flowRows * rowH;
            depth = depth + 2;
         }
         i = i + 1;
      }
      return;
   }

   var gridH = _root.HagWelcome.card._cyBot - topY;
   if (!(gridH > 120)) { gridH = 260; }
   var gap = 18;
   var colW = Math.floor((w - gap) / 2);
   var padX = 14;
   var padY = 14;
   _root.paintGridShell(c, x, topY, w, gridH, colW, gap);
   var used = new Object();
   i = 0;
   while (i < pg.opts.length)
   {
      var op0 = pg.opts[i];
      if (_root.gridRenderable(op0) && op0.gridRow >= 0)
      {
         var ec = (op0.gridColumn >= 0) ? op0.gridColumn : 0;
         if (ec > 1) { ec = 1; }
         _root.gridReserve(used, ec, op0.gridRow, _root.gridControlRows(op0));
      }
      i = i + 1;
   }

   i = 0;
   while (i < pg.opts.length)
   {
      var op = pg.opts[i];
      if (_root.gridRenderable(op))
      {
         var span = _root.gridControlRows(op);
         var col = 0;
         var row = 0;
         if (op.gridRow >= 0)
         {
            col = (op.gridColumn >= 0) ? op.gridColumn : 0;
            if (col > 1) { col = 1; }
            row = op.gridRow;
         }
         else
         {
            var slot = _root.gridNextSlot(used, span);
            col = slot.col;
            row = slot.row;
            _root.gridReserve(used, col, row, span);
         }

         var cellX = x + (col * (colW + gap)) + padX;
         var cellY = topY + padY + row * rowH;
         var cellW = colW - padX * 2;
         var cellH = _root.gridControlHeight(op, rowH);
         _root.gridPlaceControl(c, i, depth, op, cellX, cellY, cellW, cellH);
         depth = depth + 2;
      }
      i = i + 1;
   }
}
// Called by C++ (OptionRender::BuildIfNeeded) after it writes the flat model into the movie.
// Reads _root.hagPage<i>_* into _root.HAG_PAGES, rebuilds the tab strip (Welcome + one tab per
// page), and re-renders the active tab. AS2 has no object literals under FFDec, so build with new Object().
function HagBuildPages()
{
   _root.HAG_PAGES = new Array();
   var pc = Number(_root.hagPageCount);
   if (!(pc > 0)) { pc = 0; }
   var i = 0;
   while (i < pc)
   {
      var pg = new Object();
      pg.title = String(_root["hagPage" + i + "_title"]);
      var dp = Number(_root["hagPage" + i + "_doublePage"]);
      pg.doublePage = (dp == 1) ? 1 : 0;
      pg.opts = new Array();
      var oc = Number(_root["hagPage" + i + "_optCount"]);
      if (!(oc > 0)) { oc = 0; }
      var j = 0;
      while (j < oc)
      {
         var op = new Object();
         op.label = String(_root["hagPage" + i + "_opt" + j + "_label"]);
         op.type = Number(_root["hagPage" + i + "_opt" + j + "_type"]);
         op.value = Number(_root["hagPage" + i + "_opt" + j + "_value"]);
         var en = _root["hagPage" + i + "_opt" + j + "_enabled"];
         op.enabled = (en == undefined) ? 1 : (Number(en) != 0 ? 1 : 0);   // default enabled if absent
         var nt = _root["hagPage" + i + "_opt" + j + "_note"];
         op.note = (nt == undefined) ? "" : String(nt);
         op.color = Number(_root["hagPage" + i + "_opt" + j + "_color"]);      // ProgressBar fill colour
         op.fill  = Number(_root["hagPage" + i + "_opt" + j + "_fill"]);       // ProgressBar 0..1
         var btx  = _root["hagPage" + i + "_opt" + j + "_bartext"];            // ProgressBar value text
         op.bartext = (btx == undefined) ? "" : String(btx);
         var gc = Number(_root["hagPage" + i + "_opt" + j + "_gridColumn"]);
         var gr = Number(_root["hagPage" + i + "_opt" + j + "_gridRow"]);
         op.gridColumn = (gc >= 0) ? gc : -1;
         op.gridRow = (gr >= 0) ? gr : -1;
         op.pageIdx = i;
         op.optIdx = j;
         pg.opts.push(op);
         j = j + 1;
      }
      _root.HAG_PAGES.push(pg);
      i = i + 1;
   }
   // tabs: Welcome first, then one per registered page
   _root.HAG_TABS = new Array();
   _root.HAG_TABS.push("WELCOME");
   i = 0;
   while (i < _root.HAG_PAGES.length)
   {
      _root.HAG_TABS.push(_root.HAG_PAGES[i].title.toUpperCase());
      i = i + 1;
   }
   if (_root.hagActive >= _root.HAG_TABS.length) { _root.hagActive = 0; }
   var card = _root.HagWelcome.card;
   if (card.nav) { card.nav.removeMovieClip(); }
   _root.buildNav(card, card._nx, card._ny, card._nw);
   _root.showPage(_root.hagActive);
}

function buildWelcome()
{
   var W = Stage.width;
   var H = Stage.height;
   if (!(W > 200)) { W = 1280; H = 720; }

   var s = _root.createEmptyMovieClip("HagWelcome", 200);

   // ---- backdrop: vertical bg-1 -> bg-0, plus a warm gold radial glow top-right ----
   var bg = s.createEmptyMovieClip("bg", 1);
   bg.beginGradientFill("linear", [0x121013, 0x0A0A0C], [100, 100], [0, 255],
      boxM(0, 0, W, H, Math.PI / 2));
   rect(bg, 0, 0, W, H); bg.endFill();
   var glow = s.createEmptyMovieClip("glow", 2);
   glow.beginGradientFill("radial", [0x241D0F, 0x0A0A0C], [85, 0], [0, 255],
      boxM(W * 0.34, -H * 0.62, W * 1.05, H * 1.5, 0));
   rect(glow, 0, 0, W, H); glow.endFill();

   // ---- hero card ----
   var cw = 820;
   var ch = 462;
   var cx = Math.round((W - cw) / 2);
   var cy = Math.round((H - ch) / 2) - 6;
   // expose the panel rect (in _root coords) so the mouse handler can close only on outside clicks
   _root.hagX = cx; _root.hagY = cy; _root.hagW = cw; _root.hagH = ch;
   var card = s.createEmptyMovieClip("card", 10);

   // thin, soft drop shadow (two light offset layers; the old single 14px offset read as a heavy band)
   card.beginFill(0x000000, 20); rrPath(card, cx, cy + 3, cw, ch, 14); card.endFill();
   card.beginFill(0x000000, 11); rrPath(card, cx, cy + 6, cw, ch, 14); card.endFill();
   // panel fill + gold hairline border in one pass
   card.lineStyle(1, 0xE0B34A, 42, true, "none", "round", "round");
   card.beginFill(0x1A1712, 96); rrPath(card, cx, cy, cw, ch, 14); card.endFill();

   // ---- left accent: a soft glow fading right + a bright accent->accent-dim rail. Both are plain
   // full-height bars CLIPPED by a mask using the card's IDENTICAL rounded-rect path, so they
   // follow the radius-14 corners exactly and stay flush with the border (no manual corner math). ----
   var railG = card.createEmptyMovieClip("railG", 14);
   railG.beginGradientFill("linear", [0xE0B34A, 0xE0B34A], [26, 0], [0, 255], boxM(cx, cy, 30, ch, 0));
   rect(railG, cx, cy, 30, ch); railG.endFill();
   railG.beginGradientFill("linear", [0xE0B34A, 0xB8862F], [100, 100], [0, 255], boxM(cx, cy, 6, ch, Math.PI / 2));
   rect(railG, cx, cy, 6, ch); railG.endFill();
   var rmask = card.createEmptyMovieClip("rmask", 13);
   rmask.beginFill(0xFFFFFF, 100); rrPath(rmask, cx, cy, cw, ch, 14); rmask.endFill();
   railG.setMask(rmask);

   // corner flourishes (thin gold L-brackets, top-left + bottom-right) for the framed look
   var fl = card.createEmptyMovieClip("fl", 16);
   fl.lineStyle(1, 0xE0B34A, 30);
   fl.moveTo(cx + 30, cy + 22); fl.lineTo(cx + 56, cy + 22);
   fl.moveTo(cx + cw - 30, cy + ch - 22); fl.lineTo(cx + cw - 56, cy + ch - 22);

   var px = cx + 60;
   var cwid = cw - 60 - 48;

   // ---- navigation tab strip (registerable; _root.HAG_TABS holds the labels) ----
   card._nx = px; card._ny = cy + 28; card._nw = cwid;
   buildNav(card, px, cy + 28, cwid);

   // CLOSE button geometry; the button itself is drawn by showCloseButton() (Welcome tab only).
   card._btnX = px; card._btnY = cy + ch - 86; card._btnW = 152; card._btnH = 40;

   // ---- content area: the active tab's page (rebuilt by showPage on tab change) ----
   card._cx = px; card._cyTop = cy + 86; card._cw2 = cwid; card._cyBot = cy + ch - 52;  // content bottom (above footer)
   showPage(_root.hagActive);   // also (re)draws the Welcome-only CLOSE button via showCloseButton

   // footer mark (bottom-right inside the card)
   mkText(card, "foot", 24, cx + cw - 230, cy + ch - 40, 200, 20,
      "<p align='right'><font face='$EverywhereBoldFont' size='11' color='#6B6456'>HAGRYPH&nbsp;&nbsp;&#183;&nbsp;&nbsp;EST.&nbsp;MMXXVI</font></p>");
}

// ---- boot ----
var CodeObj = new Object();
Shared.GlobalFunc.MaintainTextFormat();
// Hide the vanilla Credits chrome; Menu_mc stays (carries handleInput + focus) but renders nothing.
BackMouseButton._visible = false;
BackGamepadButton._visible = false;
// tab registry: Welcome plus any option pages registered by mods (added by HagBuildPages()).
_root.HAG_TABS = ["WELCOME"];
_root.HAG_PAGES = new Array();
_root.hagActive = 0;
buildWelcome();
// Signal to C++ (OptionRender::BuildIfNeeded) that the boot has run and _root.HagBuildPages /
// _root.hagSetOption can now be used. C++ pushes the page model and calls HagBuildPages once it sees this.
_root.hagReady = true;
stop();
