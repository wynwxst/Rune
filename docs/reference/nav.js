(function(){
  var root=document.documentElement;

  /* ---- settings ---------------------------------------------------------
     There is no header bar, so the two things a reader can change about the
     page live at the top of the right rail. Both are remembered. */
  function readPref(key,fallback){
    try{ var v=localStorage.getItem(key); return v===null?fallback:v; }
    catch(e){ return fallback; }
  }
  function writePref(key,value){
    try{ localStorage.setItem(key,value); }catch(e){}
  }

  var PALETTES={auto:1,light:1,dark:1,ocean:1,forest:1,sunset:1,purple:1};
  function applyTheme(name){
    if(name==='auto') root.removeAttribute('data-theme');
    else root.setAttribute('data-theme',name);
  }
  var theme=readPref('rune-theme','auto');
  if(!PALETTES[theme]) theme='auto';
  applyTheme(theme);
  var themeSel=document.getElementById('theme');
  if(themeSel){
    themeSel.value=theme;
    themeSel.addEventListener('change',function(){
      applyTheme(themeSel.value); writePref('rune-theme',themeSel.value);
    });
  }

  var groups=[].slice.call(document.querySelectorAll('.rail-group'));
  var sections=[].slice.call(document.querySelectorAll('main > section'));
  var subLinks=[].slice.call(document.querySelectorAll('.rail-sub a'));
  var tocLinks=[].slice.call(document.querySelectorAll('.toc a'));
  var page=document.querySelector('main');

  var tocList=document.getElementById('toclist');
  var tocNav=document.querySelector('.tocnav');
  var tocFor=null;
  function buildToc(section){
    if(!tocList || !section || tocFor===section.id) return;
    tocFor=section.id;
    var hs=[].slice.call(section.querySelectorAll('h3[id]'));
    /* A page with no headings has no contents worth naming. */
    if(tocNav) tocNav.classList.toggle('empty', hs.length===0);
    tocList.innerHTML='';
    hs.forEach(function(h){
      var li=document.createElement('li');
      var a=document.createElement('a');
      a.href='#'+h.id;
      a.textContent=h.getAttribute('data-title')||h.textContent.replace('#','').trim();
      li.appendChild(a); tocList.appendChild(li);
    });
    tocLinks=[].slice.call(document.querySelectorAll('.toc a'));
  }

  /* One section at a time, or the whole document — the second setting. */
  var paged=readPref('rune-layout','paged')!=='all';
  var shown=null;

  function sectionOf(id){
    if(!id) return null;
    for(var i=0;i<sections.length;i++){
      if(sections[i].id===id) return sections[i];
    }
    var el=document.getElementById(id);
    if(!el) return null;
    return el.closest ? el.closest('main > section') : null;
  }

  /* Whichever section the reader is looking at: the one being shown when the
     document is paged, the one nearest the top of the window when it is not.
     The right rail and the left rail both key off it. */
  function setCurrent(section){
    if(!section || section===shown) return;
    shown=section;
    buildToc(section);
    groups.forEach(function(g){
      g.classList.toggle('on', g.getAttribute('data-for')===section.id);
    });
    markRails();
  }

  function show(section,anchor){
    if(!section) return;
    if(!paged){
      var el=document.getElementById(anchor||section.id);
      if(el){ el.scrollIntoView(); } else { window.scrollTo(0,0); }
      return;
    }
    if(section!==shown){
      sections.forEach(function(s){ s.classList.toggle('on', s===section); });
      setCurrent(section);
      markEdges(section);
    }
    if(anchor && anchor!==section.id){
      var target=document.getElementById(anchor);
      if(target){ target.scrollIntoView(); return; }
    }
    window.scrollTo(0,0);
  }

  function applyLayout(mode){
    paged = mode!=='all';
    if(paged){
      root.setAttribute('data-paged','');
      shown=null;
      route();
    }else{
      root.removeAttribute('data-paged');
      sections.forEach(function(s){ s.classList.remove('on'); });
      shown=null;
      markEdges(page);
      markHeading();
      markRails();
    }
  }
  applyLayout(paged?'paged':'all');

  var layoutSel=document.getElementById('layout');
  if(layoutSel){
    layoutSel.value=paged?'paged':'all';
    layoutSel.addEventListener('change',function(){
      writePref('rune-layout',layoutSel.value);
      applyLayout(layoutSel.value);
    });
  }

  /* Copy. Three routes, because a sandboxed frame may refuse the first two:
     the async clipboard, the legacy command, then selecting the text so the
     reader can finish it with a keystroke. */
  function textOf(box){
    var src=box.querySelector('.code .src');
    if(src) return src.textContent;
    var pre=box.querySelector('pre');
    return pre?pre.textContent:'';
  }
  function flash(btn,word){
    btn.setAttribute('data-done','1');
    var was=btn.dataset.label||(btn.dataset.label=btn.textContent);
    btn.textContent=word;
    clearTimeout(btn._t);
    btn._t=setTimeout(function(){
      btn.removeAttribute('data-done'); btn.textContent=was;
    },1400);
  }
  function legacyCopy(text){
    var ta=document.createElement('textarea');
    ta.value=text;
    ta.setAttribute('readonly','');
    ta.style.cssText='position:fixed;top:-1000px;opacity:0';
    document.body.appendChild(ta);
    ta.select();
    var ok=false;
    try{ ok=document.execCommand('copy'); }catch(e){}
    document.body.removeChild(ta);
    return ok;
  }
  document.addEventListener('click',function(e){
    var btn=e.target.closest?e.target.closest('.copy'):null;
    if(!btn) return;
    e.preventDefault();
    var box=btn.closest('.sample,.grammar,.shell');
    var text=textOf(box).replace(/\s+$/,'')+'\n';
    function fallback(){
      if(legacyCopy(text)){ flash(btn,'Copied'); return; }
      var target=box.querySelector('.code .src')||box.querySelector('pre');
      if(target && window.getSelection){
        var r=document.createRange();
        r.selectNodeContents(target);
        var sel=window.getSelection();
        sel.removeAllRanges(); sel.addRange(r);
        /* Selected but not copied: say which keystroke finishes the job. */
        var apple=/Mac|iPhone|iPad/.test(navigator.platform||navigator.userAgent);
        flash(btn, apple?'⌘C to copy':'Ctrl+C to copy');
      }else{
        flash(btn,'Cannot copy');
      }
    }
    if(navigator.clipboard && navigator.clipboard.writeText){
      navigator.clipboard.writeText(text).then(function(){
        flash(btn,'Copied');
      },fallback);
    }else{
      fallback();
    }
  });

  /* The rails scroll silently, so fade their bottom edge while more remains. */
  function markRails(){
    [].slice.call(document.querySelectorAll('.rail,.toc')).forEach(function(el){
      var more=el.scrollTop+el.clientHeight < el.scrollHeight-2;
      if(more) el.setAttribute('data-more','1');
      else el.removeAttribute('data-more');
    });
  }
  [].slice.call(document.querySelectorAll('.rail,.toc')).forEach(function(el){
    el.addEventListener('scroll',markRails,{passive:true});
  });

  /* Faint right edge wherever a slab has more than it can show. */
  function markEdges(scope){
    if(!scope) return;
    var wide=[].slice.call(scope.querySelectorAll(
      '.sample,.tablewrap,.grammar,.shell,.output,.diagnostic'));
    wide.forEach(function(box){
      /* A closed <details> keeps its last measurements, so ask it first. */
      var closed=box.tagName==='DETAILS' && !box.open;
      var inner=box.querySelector('.code,pre')||box;
      var over=!closed && inner.scrollWidth>inner.clientWidth+1;
      if(over) box.setAttribute('data-of','1');
      else box.removeAttribute('data-of');
    });
  }

  function route(){
    var id=(location.hash||'').replace(/^#/,'');
    var section=sectionOf(id)||sections[0];
    show(section, id);
    markHeading();
  }

  function nearestSection(y){
    var near=sections[0]||null;
    for(var i=0;i<sections.length;i++){
      if(sections[i].offsetTop<=y) near=sections[i];
    }
    return near;
  }

  function markHeading(){
    var scope=paged?shown:page;
    if(!paged) setCurrent(nearestSection(window.scrollY+160));
    if(!scope) return;
    var hs=[].slice.call((paged?shown:(shown||page)).querySelectorAll('h3[id]'));
    var y=window.scrollY+140, near=null;
    for(var j=0;j<hs.length;j++){ if(hs[j].offsetTop<=y) near=hs[j]; }
    var want=near?'#'+near.id:(shown?'#'+shown.id:'');
    subLinks.forEach(function(a){
      a.classList.toggle('here', a.getAttribute('href')===want);
    });
    tocLinks.forEach(function(a){
      a.classList.toggle('here', a.getAttribute('href')===want);
    });
  }

  /* A collapsed block has no width to measure, so remeasure when it opens. */
  document.addEventListener('toggle',function(e){
    if(e.target.matches && e.target.matches('.output,.diagnostic'))
      markEdges(paged?shown:page);
  },true);

  window.addEventListener('hashchange',route);
  window.addEventListener('resize',function(){
    markEdges(paged?shown:page);
    markRails();
  });
  var raf=false;
  window.addEventListener('scroll',function(){
    if(raf) return; raf=true;
    requestAnimationFrame(function(){ raf=false; markHeading(); });
  },{passive:true});
  route();

  /* A link to the page already showing changes no hash, so route it here. */
  document.addEventListener('click',function(e){
    var a=e.target.closest?e.target.closest('a[href^="#"]'):null;
    if(!a) return;
    var id=a.getAttribute('href').slice(1);
    var section=sectionOf(id);
    if(!paged) return;                                     /* one long page */
    if(section && section===shown && id!==shown.id) return; /* same page */
    if(section && section!==shown){ e.preventDefault(); location.hash=id; }
  });

  /* Search filters the rail by section and heading text. */
  var box=document.getElementById('q');
  var nores=document.getElementById('nores');
  if(box){
    box.addEventListener('input',function(){
      var q=box.value.trim().toLowerCase();
      var any=false;
      groups.forEach(function(g){
        var hay=(g.getAttribute('data-search')||'').toLowerCase();
        var hit=!q||hay.indexOf(q)>=0;
        g.classList.toggle('hide',!hit);
        if(hit) any=true;
        if(q){
          g.classList.add('on');
          [].slice.call(g.querySelectorAll('.rail-sub a')).forEach(function(a){
            var t=a.textContent.toLowerCase();
            a.classList.toggle('hide', t.indexOf(q)<0 &&
              (g.getAttribute('data-title')||'').toLowerCase().indexOf(q)<0);
          });
        }else{
          [].slice.call(g.querySelectorAll('.rail-sub a')).forEach(function(a){
            a.classList.remove('hide');
          });
        }
      });
      if(nores) nores.classList.toggle('on',!any);
      if(!q && shown){
        groups.forEach(function(g){
          g.classList.toggle('on', g.getAttribute('data-for')===shown.id);
        });
      }
    });
  }

  document.addEventListener('keydown',function(e){
    var typing=document.activeElement===box;
    if(e.key==='/' && !typing && box){
      e.preventDefault(); box.focus(); box.select(); return;
    }
    if(e.key==='Escape' && typing){
      box.value=''; box.dispatchEvent(new Event('input')); box.blur(); return;
    }
    if(typing || e.metaKey || e.ctrlKey || e.altKey) return;
    if(document.activeElement && document.activeElement.tagName==='SELECT') return;
    if(!paged) return;
    if(e.key==='ArrowLeft' || e.key==='ArrowRight'){
      if(!shown) return;
      var i=sections.indexOf(shown);
      var to=sections[e.key==='ArrowRight'?i+1:i-1];
      if(to){ e.preventDefault(); location.hash=to.id; }
    }
  });
})();
