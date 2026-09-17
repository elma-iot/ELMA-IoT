// Same offline language catalog and translation engine as the Android WebView.
if(new URLSearchParams(location.search).get('elmaRuntime')==='pc-designer'){
  await import('./i18n.js');
  await window.elmaDesignerReady;
  const languageSelect=document.getElementById('headerLanguageSelect'),themeSelect=document.getElementById('headerThemeSelect');
  languageSelect?.replaceChildren(...window.ElmaI18n.locales.map(locale=>new Option(locale.nativeName,locale.code)));
  let ui={};
  try{ui=(await fetch('/api/settings').then(r=>r.json())).ui||{};}catch{}
  function applyTheme(value){
    const mode=['light','dark'].includes(value)?value:'automatic';
    if(mode==='automatic')document.documentElement.removeAttribute('data-elma-theme');else document.documentElement.dataset.elmaTheme=mode;
    if(themeSelect)themeSelect.value=mode;
    return mode;
  }
  function applyLanguage(value){window.ElmaI18n.setLanguage(value);if(languageSelect)languageSelect.value=window.ElmaI18n.language;return window.ElmaI18n.language;}
  async function change(value){
    ui={...ui,...value};
    window.dispatchEvent(new CustomEvent('elma-firmware-preferences-changed',{detail:ui}));
    const response=await fetch('/api/ui/preferences',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(value)});
    if(!response.ok)throw new Error('Could not save interface preferences');
    return ui;
  }
  window.ElmaDesktopPreferences={
    setLanguage:value=>change({language:applyLanguage(value)}),
    setTheme:value=>change({theme:applyTheme(value)}),
  };
  applyLanguage(ui.language||window.ElmaI18n.language);applyTheme(ui.theme);
  languageSelect?.addEventListener('change',event=>window.ElmaDesktopPreferences.setLanguage(event.target.value).catch(console.error));
  themeSelect?.addEventListener('change',event=>window.ElmaDesktopPreferences.setTheme(event.target.value).catch(console.error));
}
