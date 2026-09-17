/** Stable identities for static fields, dialogs and dynamically added controls. */
export function installFormControlIds(root=document) {
  const documentRoot=root.ownerDocument||root;
  const fields='input,select,textarea';
  function ensure(node) {
    if(!node||![1,9].includes(node.nodeType))return;
    const controls=[...(node.matches?.(fields)?[node]:[]),...node.querySelectorAll(fields)];
    for(const field of controls){
      const existing=field.id.trim();
      if(existing&&documentRoot.getElementById(existing)===field)continue;
      const token=existing||'elma-field-'+String(field.name||field.getAttribute('aria-label')||field.type||field.tagName).toLowerCase().replace(/[^a-z0-9_-]+/g,'-').replace(/^-|-$/g,'');
      let id=token,index=2;while(documentRoot.getElementById(id))id=token+'-'+index++;
      field.id=id;
      const label=field.closest('label');if(label&&label.htmlFor===existing)label.htmlFor=id;
    }
  }
  ensure(root);
  const observer=new MutationObserver(records=>{for(const record of records)for(const node of record.addedNodes)ensure(node)});
  observer.observe(root,{childList:true,subtree:true});return observer;
}
