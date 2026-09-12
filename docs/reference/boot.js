/* Runs in <head>, before the page paints: the palette and the reading mode
   are remembered, and applying them here rather than at the end of the body
   is the difference between a page that opens in the reader's colours and
   one that flashes the default first. nav.js reads the same two keys. */
(function(){
  try{
    var root=document.documentElement, theme=null, layout=null;
    try{
      theme=localStorage.getItem('rune-theme');
      layout=localStorage.getItem('rune-layout');
    }catch(e){}
    if(theme && /^(light|dark|ocean|forest|sunset|purple)$/.test(theme))
      root.setAttribute('data-theme',theme);
    if(layout!=='all') root.setAttribute('data-paged','');
  }catch(e){}
})();
