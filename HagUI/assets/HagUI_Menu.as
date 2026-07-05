// HagUI menu class — overwrites scripts/__Packages/CreditsMenu.as inside the cloned
// Credits CLIK shell. The class SYMBOL must stay "CreditsMenu" (CreditsMenuObj.as does
// Object.registerClass("CreditsMenuObj",CreditsMenu) and the Menu_mc instance is bound to
// that export), so we keep the name and only swap the body for minimal HagUI behavior.
//
// All visuals are drawn from C++ into _root; this class is logic-only. It reuses Bethesda's
// InputDelegate -> FocusHandler -> handleInput chain so the engine delivers ESC to us, then
// closes via gfx.io.GameDelegate.call("CloseHagUI") -> our native Hag_CloseHandler -> kHide.
class CreditsMenu extends MovieClip
{
   function CreditsMenu()
   {
      super();
      // setFocus boots FocusHandler.initialize() -> InputDelegate Key.addListener, which is
      // what makes the engine deliver keyboard input to handleInput below (same as Credits).
      Mouse.addListener(this);
      gfx.managers.FocusHandler.instance.setFocus(this, 0);
   }
   function onCodeObjectInit()
   {
      // native RegisterFuncs may ping this; nothing to pull from C++ for now.
   }
   function handleInput(details, pathToFocus)
   {
      if(Shared.GlobalFunc.IsKeyPressed(details))
      {
         switch(details.navEquivalent)
         {
            case gfx.ui.NavigationCode.TAB:
            case gfx.ui.NavigationCode.ESCAPE:
               gfx.io.GameDelegate.call("CloseHagUI", []);   // ESC -> native HagMenu::Close()
               break;
         }
      }
      return true;   // consumed; no nav fallthrough
   }
   function onMouseDown()
   {
      // Close only when the click lands OUTSIDE the hero card (i.e. on the dark backdrop);
      // clicks inside the panel are ignored so future buttons/controls can handle them.
      var mx = _root._xmouse;
      var my = _root._ymouse;
      if (mx < _root.hagX || mx > _root.hagX + _root.hagW || my < _root.hagY || my > _root.hagY + _root.hagH)
      {
         gfx.io.GameDelegate.call("CloseHagUI", []);
      }
   }
}
