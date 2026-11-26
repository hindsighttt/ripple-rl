(function(){
  'use strict';
  var root=document.getElementById('replayEditor');
  if(!root)return;
  var state=null,titles={},draft=[],busy=false,plus=false,modal=null,modalDismiss=null,modalRelabel=null;
  var products=[],productIndex={},productLanguage='',loadoutPlayer=-1,loadoutTeam=0,loadoutGroup=0,loadoutPage=0,loadoutModal=null;
  var playerPage=0,playersPerPage=4,resizeFrame=0;
  var library={entries:[],page:0,total:0,platform:''},libraryLoading=false,libraryLoaded=false,refreshing=null;
  var libraryScroll=0;
  var equipment=[['body',0,'Body','Carrosserie'],['decal',1,'Decal','Decal'],['wheels',2,'Wheels','Roues'],['boost',3,'Boost','Boost'],['antenna',4,'Antenna','Antenne'],['topper',5,'Topper','Accessoire'],['trail',14,'Trail','Trainee'],['goalExplosion',15,'Goal explosion','Explosion de but'],['engineAudio',13,'Engine audio','Son du moteur'],['banner',16,'Banner','Banniere'],['anthem',18,'Anthem','Hymne'],['border',20,'Avatar border','Bordure d\'avatar']];
  var paints=[['Unpainted','Non peint',''],['Crimson','Pourpre','990000'],['Lime','Citron vert','7fff00'],['Black','Noir','0d0d0d'],['Sky Blue','Bleu ciel','057fcc'],['Cobalt','Cobalt','001fa8'],['Burnt Sienna','Terre de Sienne','8b3f22'],['Forest Green','Vert foret','006b2d'],['Purple','Violet','6200b3'],['Pink','Rose','e86ba6'],['Orange','Orange','ff7f00'],['Grey','Gris','888888'],['Titanium White','Blanc titane','eeeeee'],['Saffron','Safran','e6cc00'],['Gold','Or','d5ae3c'],['Rose Gold','Or rose','c98787'],['White Gold','Or blanc','e1dbbc'],['Onyx','Onyx','333333'],['Platinum','Platine','b0b9c4'],['Sky Blue Glow','Bleu ciel lumineux','57bfff'],['Cobalt Glow','Cobalt lumineux','5570ff'],['Burnt Sienna Glow','Terre de Sienne lumineuse','bd7048'],['Forest Green Glow','Vert foret lumineux','46b573'],['Lime Glow','Citron vert lumineux','acff55'],['Orange Glow','Orange lumineux','ffad46'],['Pink Glow','Rose lumineux','ff99cf'],['Purple Glow','Violet lumineux','ae60ff'],['Crimson Glow','Pourpre lumineux','ff5555'],['Titanium White Glow','Blanc titane lumineux','ffffff'],['Saffron Glow','Safran lumineux','ffff66']];
  function clone(value){return JSON.parse(JSON.stringify(value));}
  function nameOf(p,i){var name=String(p.name==null?'':p.name).replace(/\u200B/g,'').trim();return name||state.players[i].name;}
  function changed(p,i){return nameOf(p,i)!==state.players[i].name||p.titleId!==state.players[i].titleId||JSON.stringify(p.loadouts)!==JSON.stringify(state.players[i].loadouts);}
  function text(en,fr){return typeof lang!=='undefined'&&lang==='en'?en:fr;}
  function esc(value){return String(value==null?'':value).replace(/[&<>"']/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c];});}
  function byId(id){return document.getElementById(id);}
  function status(message,error){var node=byId('replayStatus');node.textContent=message||'';node.classList.toggle('error',!!error);}
  function pending(button,active){
    var node=byId(button);clearTimeout(node.replayPendingTimer);node.classList.remove('replay-pending');
    if(active)node.replayPendingTimer=setTimeout(function(){node.classList.add('replay-pending');},250);
  }
  function libraryPlaceholder(){
    return '<div class="replay-library-placeholder" aria-hidden="true">'+Array.from({length:6},function(){return '<div class="replay-placeholder-row"><span></span><div><span></span><span></span></div><span></span></div>';}).join('')+'</div>';
  }
  function changes(){return state?draft.filter(changed).length:0;}
  function availability(){
    var disabled=busy||libraryLoading||!plus;
    byId('replayOpen').disabled=disabled;byId('replayDrop').disabled=disabled;
    byId('replaySaveCopy').disabled=disabled||!changes();
    byId('replayReplace').disabled=disabled||!changes()||!state||!state.canReplace;
    byId('replayResetAll').disabled=disabled||!changes();
    byId('replayClose').disabled=disabled;
    byId('replayLibraryRefresh').disabled=disabled;
    root.querySelectorAll('#replayPages button,#replayLibraryPages button').forEach(function(n){n.disabled=disabled||n.dataset.pageDisabled==='1';});
    root.querySelectorAll('[data-replay-entry]').forEach(function(n){n.disabled=disabled||n.dataset.readable!=='1';});
    document.querySelectorAll('#replayRows button,#replayRows input,.replay-loadout-dialog [data-replay-unavailable]').forEach(function(n){n.disabled=disabled||n.dataset.replayUnavailable==='1';});
    byId('replayChanges').textContent=changes()?changes()+' '+text('modified','modifie(s)'):'';
    root.setAttribute('aria-busy',busy||libraryLoading?'true':'false');
  }
  var replayMaps={
    "stadium_p":"DFH Stadium",
    "stadium_day_p":"DFH Stadium (Day)",
    "stadium_foggy_p":"DFH Stadium (Stormy)",
    "stadium_winter_p":"DFH Stadium (Snowy)",
    "stadium_race_day_p":"DFH Stadium (Circuit)",
    "stadium_10a_p":"DFH Stadium (Anniversary)",
    "eurostadium_p":"Mannfield",
    "eurostadium_night_p":"Mannfield (Night)",
    "eurostadium_rainy_p":"Mannfield (Stormy)",
    "eurostadium_snownight_p":"Mannfield (Snowy)",
    "eurostadium_snow_p":"Mannfield (Snowy)",
    "eurostadium_dusk_p":"Mannfield (Dusk)",
    "cs_p":"Champions Field",
    "cs_day_p":"Champions Field (Day)",
    "cs_hw_p":"Rivals Arena",
    "bb_p":"Champions Field (NFL)",
    "swoosh_p":"Champions Field (Nike FC)",
    "trainstation_p":"Urban Central",
    "trainstation_night_p":"Urban Central (Night)",
    "trainstation_dawn_p":"Urban Central (Dawn)",
    "haunted_trainstation_p":"Urban Central (Haunted)",
    "park_p":"Beckwith Park",
    "park_night_p":"Beckwith Park (Midnight)",
    "park_rainy_p":"Beckwith Park (Stormy)",
    "park_snowy_p":"Beckwith Park (Snowy)",
    "park_bman_p":"Beckwith Park (Gotham Night)",
    "utopiastadium_p":"Utopia Coliseum",
    "utopiastadium_dusk_p":"Utopia Coliseum (Dusk)",
    "utopiastadium_snow_p":"Utopia Coliseum (Snowy)",
    "utopiastadium_lux_p":"Utopia Coliseum (Gilded)",
    "wasteland_s_p":"Wasteland",
    "wasteland_night_s_p":"Wasteland (Night)",
    "wasteland_grs_p":"Wasteland (Pitched)",
    "wasteland_p":"Badlands",
    "wasteland_night_p":"Badlands (Night)",
    "neotokyo_standard_p":"Neo Tokyo",
    "neotokyo_p":"Tokyo Underpass",
    "neotokyo_toon_p":"Neo Tokyo (Comic)",
    "neotokyo_hax_p":"Neo Tokyo (Hacked)",
    "neotokyo_arcade_p":"Neo Tokyo (Arcade)",
    "underwater_p":"AquaDome",
    "aquarium_p":"AquaDome",
    "underwater_grs_p":"AquaDome (Shallows)",
    "arc_standard_p":"Starbase ARC",
    "arc_darc_p":"Starbase ARC (Aftermath)",
    "arc_p":"Arctagon",
    "farm_p":"Farmstead",
    "farm_night_p":"Farmstead (Night)",
    "farm_hw_p":"Farmstead (Spooky)",
    "farm_grs_p":"Farmstead (Pitched)",
    "beach_p":"Salty Shores",
    "beach_night_p":"Salty Shores (Night)",
    "beach_night_grs_p":"Salty Shores (Salty Fest)",
    "chn_stadium_p":"Forbidden Temple",
    "chn_stadium_day_p":"Forbidden Temple (Day)",
    "fni_stadium_p":"Forbidden Temple (Fire & Ice)",
    "music_p":"Neon Fields",
    "outlaw_p":"Deadeye Canyon",
    "outlaw_oasis_p":"Deadeye Canyon (Oasis)",
    "ff_dusk_p":"Estadio Vida (Dusk)",
    "street_p":"Sovereign Heights",
    "woods_p":"Drift Woods",
    "woods_night_p":"Drift Woods (Night)",
    "uf_day_p":"Futura Garden",
    "mall_day_p":"Boostfield Mall",
    "paname_dusk_p":"Parc de Paris",
    "hoopsstadium_p":"Dunk House",
    "hoopsstreet_p":"The Block",
    "shattershot_p":"Core 707",
    "throwbackstadium_p":"Throwback Stadium",
    "throwbackhockey_p":"Throwback Stadium (Snowy)",
    "ko_quadron_p":"Quadron",
    "ko_calavera_p":"Calavera",
    "ko_carbon_p":"Carbon",
    "labs_circlepillars_p":"Pillars",
    "labs_cosmic_v4_p":"Cosmic",
    "labs_doublegoal_v2_p":"Double Goal",
    "labs_octagon_02_p":"Octagon",
    "labs_underpass_p":"Underpass",
    "labs_utopia_p":"Utopia Retro",
    "labs_basin_p":"Basin",
    "labs_corridor_p":"Corridor",
    "labs_holyfield_p":"Loophole",
    "labs_galleon_p":"Galleon",
    "labs_galleon_mast_p":"Galleon Retro",
    "labs_pillarglass_p":"Hourglass",
    "labs_pillarheat_p":"Barricade",
    "labs_pillarwings_p":"Colossus",
    "labs_holyfield_space_p":"Holyfield",
    "labs_octagon_b2b_02_p":"Roadblock",
    "labs_4v4_arena15_eurostadium_night_p":"Mannfield (Quads)",
    "labs_4v4_arena15_blackout_p":"Midnight Metro (Quads)",
    "labs_4v4_arena15_retro_p":"Sunset Dunes (Quads)"
  };
  function replayMap(id){
    var key=String(id||'').toLowerCase();
    return Object.prototype.hasOwnProperty.call(replayMaps,key)?replayMaps[key]:id||'';
  }
  function replayName(entry){
    var map=replayMap(entry.map),name=entry.name||'',type=entry.matchType==='Online'?text('Online','En ligne'):entry.matchType==='Private'?text('Private','Prive'):'';
    if(name){
      var parts=name.split(' / ');
      if(parts[0].toLowerCase()===String(entry.map||'').toLowerCase()&&map!==entry.map&&(parts.length===1||(parts.length===2&&/^(Online|Private)$/i.test(parts[1])))){
        return parts.length===1?map:[map,/^Online$/i.test(parts[1])?text('Online','En ligne'):text('Private','Prive')].join(' / ');
      }
      return name;
    }
    return map?[map,type].filter(Boolean).join(' / '):text('Untitled replay','Replay sans nom');
  }
  function replayDate(entry){
    var raw=entry.date||'',match=/^(\d{4})-(\d{2})-(\d{2}) (\d{2})[-:](\d{2})[-:](\d{2})$/.exec(raw);
    var value=match?new Date(+match[1],+match[2]-1,+match[3],+match[4],+match[5],+match[6]):new Date(entry.modified||'');
    return isNaN(value.getTime())?'':value.toLocaleString(text('en-GB','fr-FR'),{day:'2-digit',month:'short',year:'numeric',hour:'2-digit',minute:'2-digit'});
  }
  function replayDuration(seconds){return typeof seconds==='number'&&isFinite(seconds)?Math.floor(seconds/60)+':'+String(Math.floor(seconds%60)).padStart(2,'0'):'';}
  function renderLibrary(){
    var count=library.total||0,entries=library.entries||[],rows=byId('replayLibraryRows');
    byId('replayLibraryHeading').textContent=text('Saved replays','Replays enregistres')+(library.platform?' / '+library.platform:'');
    byId('replayLibraryRefresh').title=text('Refresh replays','Actualiser les replays');byId('replayLibraryRefresh').setAttribute('aria-label',byId('replayLibraryRefresh').title);
    byId('replayLibraryCount').textContent=libraryLoaded?count+' '+(count===1?'replay':'replays'):'';
    rows.setAttribute('aria-busy',libraryLoading?'true':'false');
    rows.innerHTML=entries.map(function(entry){
      var first=entry.primaryTeam===1?1:0,score=[entry.blueScore,entry.orangeScore],known=Number.isInteger(score[0])&&Number.isInteger(score[1]);
      var result=known?'<span class="replay-score" data-team="'+first+'">'+score[first]+'</span><span class="replay-score-divider">:</span><span class="replay-score" data-team="'+(1-first)+'">'+score[1-first]+'</span>':'<span class="replay-unknown">?</span>';
      var detail=entry.readable?[replayMap(entry.map),replayDate(entry)].filter(Boolean).join(' / '):text('This replay could not be read','Ce replay est illisible');
      return '<div role="listitem"><button class="replay-library-entry" data-replay-entry="'+esc(entry.id)+'" data-readable="'+(entry.readable?'1':'0')+'" type="button" title="'+esc(text('Edit: ','Modifier : ')+replayName(entry))+'"><span class="replay-mode">'+(entry.teamSize?entry.teamSize+'v'+entry.teamSize:'?')+'</span><span class="replay-library-info"><strong>'+esc(replayName(entry))+'</strong><small>'+esc(detail)+'</small></span><span class="replay-library-result"><span>'+result+'</span><small>'+esc([replayDuration(entry.seconds),entry.forfeit?text('Forfeit','Abandon'):''].filter(Boolean).join(' / '))+'</small></span><i class="ph ph-caret-right" aria-hidden="true"></i></button></div>';
    }).join('')||(plus&&(libraryLoading||!libraryLoaded)?libraryPlaceholder():'<div class="replay-empty">'+(!plus?text('Available with Plus','Disponible avec Plus'):text('No saved replays for this platform','Aucun replay enregistre pour cette plateforme'))+'</div>');
    var pages=byId('replayLibraryPages'),total=Math.max(1,Math.ceil(count/20));pages.innerHTML=pageButtons(library.page,total,'data-library-page');pages.style.visibility=total>1?'visible':'hidden';
    if(!state)rows.scrollTop=libraryScroll;
  }
  async function refreshLibrary(page,refresh){
    if(busy||libraryLoading||!plus||state)return;
    libraryLoading=true;pending('replayLibraryRefresh',true);render();
    try{var result=await window.rippleCall('misc.replay.list',{page:Math.max(0,page||0),refresh:!!refresh});if(result.page!==library.page||result.platform!==library.platform)libraryScroll=0;library=result;libraryLoaded=true;status('');}
    catch(e){library={entries:[],page:0,total:0,platform:''};libraryScroll=0;libraryLoaded=true;status(String(e.message||e),true);}
    finally{libraryLoading=false;pending('replayLibraryRefresh',false);render();}
  }
  function titleLabel(id,stat){
    if(!id)return text('No title','Aucun titre');
    var entry=titles[id];return entry&&entry.Text?entry.Text.replace(/\{StatValue\}/g,String(stat||0)):id;
  }
  function pageButtons(page,count,attribute){
    return '<button class="btn sm replay-icon" '+attribute+'="-1" data-page-disabled="'+(page===0?'1':'0')+'" '+(page===0?'disabled ':'')+'aria-label="'+text('Previous page','Page precedente')+'"><i class="ph ph-caret-left" aria-hidden="true"></i></button><span class="replay-page-number">'+(page+1)+' / '+count+'</span><button class="btn sm replay-icon" '+attribute+'="1" data-page-disabled="'+(page+1>=count?'1':'0')+'" '+(page+1>=count?'disabled ':'')+'aria-label="'+text('Next page','Page suivante')+'"><i class="ph ph-caret-right" aria-hidden="true"></i></button>';
  }
  function fitPlayers(){
    if(!state||!root.getClientRects().length)return;
    var rows=byId('replayRows'),height=rows.clientHeight,rowHeight=parseFloat(getComputedStyle(rows).getPropertyValue('--replay-row-height'))||64;
    if(height<=0)return;
    var count=Math.max(1,Math.floor((height+6)/(rowHeight+6)));
    if(count!==playersPerPage){playersPerPage=count;renderRows();availability();}
  }
  function renderRows(){
    if(!state)return;
    var count=Math.max(1,Math.ceil(draft.length/playersPerPage));
    playerPage=Math.min(playerPage,count-1);
    byId('replayRows').innerHTML=draft.slice(playerPage*playersPerPage,(playerPage+1)*playersPerPage).map(function(p,local){
      var i=playerPage*playersPerPage+local,original=state.players[i];
      var teamLabel=original.team===0?text('Blue','Bleue'):original.team===1?text('Orange','Orange'):'';
      var platform=original.platform==='PS4'?'PlayStation':original.platform;
      return '<div class="replay-row'+(changed(p,i)?' is-changed':'')+'" data-replay-player="'+i+'"><div class="replay-player"><input class="replay-name" data-replay-name="'+i+'" value="'+esc(p.name)+'" maxlength="32" spellcheck="false" autocomplete="off" aria-label="'+esc(text('Name of ','Pseudo de ')+original.name)+'" title="'+esc(text('Rename this player','Renommer ce joueur'))+'"><small>'+esc(platform)+(teamLabel?'<span class="replay-team-badge" data-team="'+original.team+'">'+teamLabel+'</span>':'')+'</small></div>'+
        '<button class="btn replay-title-btn" data-replay-title="'+i+'" type="button" aria-label="'+esc(text('Title for ','Titre de ')+original.name)+'" title="'+esc(titleLabel(p.titleId,p.statValue))+'"><span><small>'+text('Title','Titre')+'</small><span>'+esc(titleLabel(p.titleId,p.statValue))+'</span></span><i class="ph ph-caret-down" aria-hidden="true"></i></button>'+
        '<div class="replay-row-tools"><button class="btn sm replay-loadout-btn" data-replay-loadout="'+i+'" data-replay-unavailable="'+(playedTeam(p)>=0?'0':'1')+'" type="button" aria-label="'+esc(text('Loadout for ','Loadout de ')+original.name)+'" aria-haspopup="dialog" aria-expanded="'+(loadoutPlayer===i)+'">Loadout</button><button class="btn sm replay-icon" data-replay-reset="'+i+'" type="button" title="'+text('Reset this player','Reinitialiser ce joueur')+'" aria-label="'+esc(text('Reset ','Reinitialiser ')+original.name)+'"><i class="ph ph-arrow-counter-clockwise" aria-hidden="true"></i></button></div></div>';
    }).join('');
    byId('replayPlayerCount').textContent=draft.length+' '+text(draft.length===1?'player':'players',draft.length===1?'joueur':'joueurs');
    var pages=byId('replayPages');pages.innerHTML=pageButtons(playerPage,count,'data-replay-page');pages.style.visibility=count>1?'visible':'hidden';
    pages.setAttribute('aria-label',text('Players','Joueurs'));
  }
  function render(){
    var labels={replayHeading:text('Replay editor','Editeur de replay'),replayOpenText:text('Open replay','Ouvrir un replay'),
      replayDropText:text('Open or drop a replay','Ouvrir ou deposer un replay'),replayDropLimit:text('.replay files, up to 32 MB','Fichiers .replay, jusqu a 32 Mo'),
      replaySaveCopyText:text('Save a copy','Enregistrer une copie'),replayReplaceText:text('Replace original','Remplacer l\'original')};
    Object.keys(labels).forEach(function(id){byId(id).textContent=labels[id];});
    var tab=document.querySelector('[data-sub="m-replay"] [data-i18n]');if(tab)tab.textContent=labels.replayHeading;
    byId('replayClose').title=text('Close replay','Fermer le replay');byId('replayClose').setAttribute('aria-label',byId('replayClose').title);
    byId('replayResetAll').title=text('Reset all changes','Annuler toutes les modifications');byId('replayResetAll').setAttribute('aria-label',byId('replayResetAll').title);
    byId('replayContent').hidden=!state;byId('replayClose').hidden=!state;
    byId('replayDrop').hidden=!!state||libraryLoading||library.entries.length>0||!plus;
    byId('replayLibrary').hidden=!!state;
    renderLibrary();
    if(state){byId('replayReplace').hidden=!state.canReplace;renderRows();}
    renderLoadout();availability();
    cancelAnimationFrame(resizeFrame);resizeFrame=requestAnimationFrame(fitPlayers);
  }
  function accept(result){
    state=result&&result.session?result:null;
    draft=state?state.players.map(clone):[];loadoutPlayer=-1;playerPage=0;
    render();
  }
  function dismiss(){var node=modal,finish=modalDismiss;modal=null;modalDismiss=null;modalRelabel=null;if(node)node.remove();if(finish)finish();}
  function ask(titleEn,titleFr,messageEn,messageFr){
    var title=text(titleEn,titleFr),message=text(messageEn,messageFr);
    return new Promise(function(resolve){
      dismiss();var prior=document.activeElement;
      modal=document.createElement('div');modal.className='modal-back show replay-modal';
      modal.innerHTML='<div class="modal-card replay-picker" role="dialog" aria-modal="true" aria-label="'+esc(title)+'"><div class="modal-head"><div class="modal-title">'+esc(title)+'</div></div><div class="modal-body"><p>'+esc(message)+'</p><div class="replay-actions"><button class="btn" data-answer="no">'+text('Cancel','Annuler')+'</button><button class="btn primary" data-answer="yes">'+text('Continue','Continuer')+'</button></div></div></div>';
      document.querySelector('.win').appendChild(modal);
      modalRelabel=function(){
        var title=text(titleEn,titleFr);
        modal.querySelector('[role="dialog"]').setAttribute('aria-label',title);
        modal.querySelector('.modal-title').textContent=title;
        modal.querySelector('.modal-body p').textContent=text(messageEn,messageFr);
        modal.querySelector('[data-answer="no"]').textContent=text('Cancel','Annuler');
        modal.querySelector('[data-answer="yes"]').textContent=text('Continue','Continuer');
      };
      function done(value){document.removeEventListener('keydown',key);modalDismiss=null;dismiss();if(prior&&prior.isConnected)prior.focus();resolve(value);}
      function key(e){if(e.key==='Escape'){e.preventDefault();done(false);}if(e.key==='Tab'){var buttons=modal.querySelectorAll('button');if(e.shiftKey&&e.target===buttons[0]){e.preventDefault();buttons[buttons.length-1].focus();}else if(!e.shiftKey&&e.target===buttons[buttons.length-1]){e.preventDefault();buttons[0].focus();}}}
      modalDismiss=function(){done(false);};
      modal.addEventListener('click',function(e){var button=e.target.closest('[data-answer]');if(button)done(button.dataset.answer==='yes');else if(e.target===modal)done(false);});
      document.addEventListener('keydown',key);modal.querySelector('[data-answer="no"]').focus();
    });
  }
  async function discard(){return !changes()||await ask('Unsaved changes','Modifications non enregistrees','Discard the current changes?','Abandonner les modifications actuelles ?');}
  async function open(payload){
    if(busy||libraryLoading||!plus)return;
    if(!state)libraryScroll=byId('replayLibraryRows').scrollTop;
    busy=true;availability();
    try{if(!await discard())return;status('');pending('replayOpen',true);var r=await window.rippleCall('misc.replay.open',payload||{});if(!r.cancelled){accept(r);status('');}else status('');}
    catch(e){status(String(e.message||e),true);}
    finally{busy=false;pending('replayOpen',false);availability();}
  }
  async function save(replace){
    if(busy||!plus||!state||!changes())return;
    busy=true;availability();
    try{
      if(replace&&!await ask('Replace original replay','Remplacer le replay original','The original replay will be replaced without a backup.','Le replay original sera remplace sans sauvegarde.'))return;
      status(text('Verifying and saving...','Verification et enregistrement...'));
      var r=await window.rippleCall('misc.replay.save',{session:state.session,replaceOriginal:replace,edits:draft.map(function(p,i){if(!changed(p,i))return null;var edit={key:p.key,titleId:p.titleId},name=nameOf(p,i);if(name!==state.players[i].name)edit.name=name;if(p.loadouts)edit.loadouts=p.loadouts;return edit;}).filter(Boolean)});
      if(r.cancelled){status('');return;}
      if(r.replaced)accept(r.state);
      status('');
    }catch(e){status(String(e.message||e),true);}
    finally{busy=false;availability();}
  }
  function trapPicker(e,options,search){
    if(!modal)return;
    if(e.key==='Tab'){
      var nodes=Array.from(modal.querySelectorAll('input,select,button:not(:disabled)')).filter(function(n){return n.getClientRects().length;});
      if(e.shiftKey&&e.target===nodes[0]){e.preventDefault();nodes[nodes.length-1].focus();}
      else if(!e.shiftKey&&e.target===nodes[nodes.length-1]){e.preventDefault();nodes[0].focus();}
    }
    if(e.key==='ArrowDown'&&e.target===search){e.preventDefault();var first=options.querySelector('button');if(first)first.focus();}
    else if((e.key==='ArrowDown'||e.key==='ArrowUp')&&options.contains(e.target)){
      var buttons=Array.from(options.querySelectorAll('button')),index=buttons.indexOf(e.target),columns=getComputedStyle(options).gridTemplateColumns.split(' ').length;
      var next=index+(e.key==='ArrowDown'?columns:-columns);e.preventDefault();
      if(next<0&&search)search.focus();else if(buttons[next])buttons[next].focus();
    }
  }
  function pickerSize(kind){
    var area=document.querySelector('.win').getBoundingClientRect(),narrow=area.width<560;
    var columns=kind==='product'?(narrow?2:3):kind==='paint'?(narrow?3:5):(narrow?1:2);
    var height=kind==='product'?176:kind==='paint'?44:112;
    return Math.max(1,Math.min(kind==='product'?3:6,Math.floor((area.height-(kind==='title'?276:230))/height)))*columns;
  }
  function pickerFooter(){return '<div class="replay-picker-footer"><span class="replay-result-count" role="status"></span><nav class="replay-pages" aria-label="'+text('Results','Resultats')+'"></nav></div>';}
  function updatePickerPage(node,total,page,size){
    node.querySelector('.replay-result-count').textContent=total+' '+text(total===1?'result':'results',total===1?'resultat':'resultats');
    var pager=node.querySelector('.replay-pages'),count=Math.max(1,Math.ceil(total/size));
    pager.innerHTML=pageButtons(page,count,'data-picker-page');pager.style.visibility=count>1?'visible':'hidden';
  }
  function focusPickerPage(direction){
    var target=modal.querySelector('[data-picker-page="'+direction+'"]');
    if(!target||target.disabled){target=modal.querySelector('.replay-page-number');if(target)target.tabIndex=-1;}
    if(target)target.focus({preventScroll:true});
  }
  function titleStyle(id){
    var category=(titles[id]||{}).Category,colors=(window.RIPPLE_CATS||{})[category]||{},style='';
    if(/^[a-f0-9]{6}$/i.test(colors.Color||''))style+='color:#'+colors.Color+';';
    if(/^[a-f0-9]{6}$/i.test(colors.GlowColor||''))style+='text-shadow:0 0 10px #'+colors.GlowColor+';';
    return style;
  }
  function picker(index){
    if(busy||!plus)return;
    dismiss();var prior=document.activeElement,page=0,size=pickerSize('title');
    modal=document.createElement('div');modal.className='modal-back show replay-modal';
    modal.innerHTML='<div class="modal-card replay-picker replay-title-picker" role="dialog" aria-modal="true" aria-label="'+text('Choose a title','Choisir un titre')+'"><div class="modal-head"><div class="replay-dialog-heading"><div class="modal-title">'+text('Choose a title','Choisir un titre')+'</div><small>'+esc(state.players[index].name)+'</small></div><button class="btn replay-icon" data-title-close aria-label="'+text('Close','Fermer')+'"><i class="ph ph-x" aria-hidden="true"></i></button></div><div class="modal-body"><div class="field"><i class="ph ph-magnifying-glass" aria-hidden="true"></i><input id="replayTitleSearch" aria-label="'+text('Search titles','Rechercher un titre')+'" placeholder="'+text('Search titles','Rechercher un titre')+'" autocomplete="off"></div><div class="dd replay-category"><button id="replayTitleCategory" class="dd-btn" type="button" aria-haspopup="listbox" aria-expanded="false" aria-controls="replayTitleCategories" aria-label="'+text('Category','Categorie')+'"><span class="dd-label"></span><i class="ph ph-caret-down" aria-hidden="true"></i></button><div id="replayTitleCategories" class="dd-menu" role="listbox" aria-label="'+text('Category','Categorie')+'" hidden></div></div><div class="replay-options replay-title-options" role="listbox" aria-label="'+text('Titles','Titres')+'"></div></div>'+pickerFooter()+'</div>';
    document.querySelector('.win').appendChild(modal);
    var options=modal.querySelector('.replay-options'),search=modal.querySelector('input'),current=draft[index].titleId;
    var category=modal.querySelector('.replay-category'),categoryButton=category.querySelector('button'),categoryMenu=category.querySelector('.dd-menu'),categoryValue='',tc=window.rippleTitleCats;
    var ids=[''].concat(Object.keys(titles).sort(function(a,b){return tc.compare(a,b,titles);}));
    if(current&&ids.indexOf(current)<0)ids.unshift(current);
    var categories=Array.from(new Set(ids.map(function(id){return tc.group((titles[id]||{}).Category);}).filter(Boolean)));
    categoryButton.querySelector('.dd-label').textContent=text('All categories','Toutes les categories');
    categoryMenu.innerHTML=[''].concat(categories).map(function(c){return '<button class="dd-opt'+(c?'':' on')+'" type="button" role="option" tabindex="-1" aria-selected="'+(!c)+'" data-replay-category="'+esc(c)+'"><span>'+esc(c?tc.name(c):text('All categories','Toutes les categories'))+'</span></button>';}).join('');
    function closeCategory(focus){category.classList.remove('open');categoryMenu.hidden=true;categoryButton.setAttribute('aria-expanded','false');if(focus)categoryButton.focus();}
    function openCategory(){
      categoryMenu.style.maxHeight=Math.max(72,Math.min(280,modal.querySelector('.modal-body').getBoundingClientRect().bottom-categoryButton.getBoundingClientRect().bottom-18))+'px';
      categoryMenu.hidden=false;category.classList.add('open');categoryButton.setAttribute('aria-expanded','true');
      var selected=categoryMenu.querySelector('[aria-selected="true"]')||categoryMenu.querySelector('button');selected.focus();selected.scrollIntoView({block:'nearest'});
    }
    category.addEventListener('click',function(e){
      var choice=e.target.closest('[data-replay-category]');
      if(choice){e.stopPropagation();categoryValue=choice.dataset.replayCategory;categoryButton.querySelector('.dd-label').textContent=choice.textContent;categoryMenu.querySelectorAll('button').forEach(function(button){var selected=button===choice;button.classList.toggle('on',selected);button.setAttribute('aria-selected',String(selected));});closeCategory(true);page=0;list();}
      else if(e.target.closest('#replayTitleCategory')){e.stopPropagation();if(categoryMenu.hidden)openCategory();else closeCategory(true);}
    });
    category.addEventListener('keydown',function(e){
      if(e.key==='Escape'&&!categoryMenu.hidden){e.preventDefault();e.stopPropagation();closeCategory(true);return;}
      if(e.key==='Tab'&&!categoryMenu.hidden){e.preventDefault();closeCategory(false);(e.shiftKey?search:options.querySelector('button')||modal.querySelector('[data-title-close]')).focus();return;}
      if(['ArrowDown','ArrowUp','Home','End'].indexOf(e.key)<0)return;
      e.preventDefault();e.stopPropagation();
      if(categoryMenu.hidden){openCategory();return;}
      var choices=Array.from(categoryMenu.querySelectorAll('button')),index=choices.indexOf(document.activeElement);
      var next=e.key==='Home'?0:e.key==='End'?choices.length-1:Math.max(0,Math.min(choices.length-1,index+(e.key==='ArrowDown'?1:-1)));
      choices[next].focus();choices[next].scrollIntoView({block:'nearest'});
    });
    function list(){
      var query=search.value.trim().toLowerCase(),rows=ids.filter(function(id){return (!categoryValue||tc.group((titles[id]||{}).Category)===categoryValue)&&(!query||titleLabel(id,draft[index].statValue).toLowerCase().indexOf(query)>=0||id.toLowerCase().indexOf(query)>=0);});
      page=Math.min(page,Math.max(0,Math.ceil(rows.length/size)-1));
      options.innerHTML=rows.slice(page*size,(page+1)*size).map(function(id){return '<button type="button" class="tcard'+(id===current?' sel':'')+'" role="option" aria-selected="'+(id===current)+'" data-title-id="'+esc(id)+'" title="'+esc(titleLabel(id,draft[index].statValue))+'"><span class="tt" style="'+titleStyle(id)+'">'+esc(titleLabel(id,draft[index].statValue))+'</span>'+(id===current?'<i class="ph ph-check replay-selected" aria-hidden="true"></i>':'')+'</button>';}).join('')||'<div class="replay-empty">'+text('No matching title','Aucun titre correspondant')+'</div>';
      updatePickerPage(modal,rows.length,page,size);
    }
    modalRelabel=function(){
      var heading=text('Choose a title','Choisir un titre');
      modal.querySelector('[role="dialog"]').setAttribute('aria-label',heading);
      modal.querySelector('.modal-title').textContent=heading;
      modal.querySelector('[data-title-close]').setAttribute('aria-label',text('Close','Fermer'));
      search.placeholder=text('Search titles','Rechercher un titre');search.setAttribute('aria-label',search.placeholder);
      categoryButton.setAttribute('aria-label',text('Category','Categorie'));
      categoryMenu.setAttribute('aria-label',text('Category','Categorie'));
      if(!categoryValue)categoryButton.querySelector('.dd-label').textContent=text('All categories','Toutes les categories');
      categoryMenu.querySelector('[data-replay-category=""] span').textContent=text('All categories','Toutes les categories');
      categoryMenu.querySelectorAll('[data-replay-category]').forEach(function(b){if(b.dataset.replayCategory)b.querySelector('span').textContent=tc.name(b.dataset.replayCategory);});
      if(categoryValue)categoryButton.querySelector('.dd-label').textContent=tc.name(categoryValue);
      options.setAttribute('aria-label',text('Titles','Titres'));
      modal.querySelector('.replay-pages').setAttribute('aria-label',text('Results','Resultats'));
      list();
    };
    function resize(){var wasOpen=!categoryMenu.hidden;closeCategory(wasOpen);size=pickerSize('title');list();}
    function close(){window.removeEventListener('resize',resize);document.removeEventListener('keydown',key);modalDismiss=null;dismiss();if(prior&&prior.isConnected)prior.focus();else if(loadoutModal)loadoutModal.querySelector('button:not(:disabled)').focus();}
    modalDismiss=close;
    function key(e){if(e.key==='Escape'){e.preventDefault();close();return;}trapPicker(e,options,search);}
    modal.addEventListener('click',function(e){if(!category.contains(e.target))closeCategory(false);var choice=e.target.closest('[data-title-id]'),pager=e.target.closest('[data-picker-page]');if(choice){draft[index].titleId=choice.dataset.titleId;close();render();var button=root.querySelector('[data-replay-title="'+index+'"]');if(button)button.focus();}else if(pager&&!pager.disabled){page+=+pager.dataset.pickerPage;list();focusPickerPage(pager.dataset.pickerPage);}else if(e.target===modal||e.target.closest('[data-title-close]'))close();});
    search.addEventListener('input',function(){page=0;list();});window.addEventListener('resize',resize);document.addEventListener('keydown',key);list();search.focus();
  }
  function paintLabel(id){return paints[id]?text(paints[id][0],paints[id][1]):String(id);}
  function paintChip(id){var color=paints[id]&&paints[id][2];return '<span class="replay-swatch"'+(color?' style="background:#'+color+'"':'')+'>'+(color?'':'<i class="ph ph-x" aria-hidden="true"></i>')+'</span>';}
  function productLabel(id){return productIndex[id]?productIndex[id].name:id?text('Product #','Produit n. ')+id:text('None','Aucun');}
  function thumbnail(id){var product=productIndex[id];if(!product||!product.asset)return '';var key=String(product.asset).split('.').pop().toLowerCase();return '<img src="https://cdn.jsdelivr.net/gh/itemchanger/assets@main/assets/'+encodeURIComponent(key)+'.png" alt="" loading="lazy" decoding="async">';}
  function productCard(product,current){
    var categories={0:'bodies',1:'decals',2:'wheels',3:'boosts',4:'antennas',5:'toppers',7:'paintfinishes',13:'enginesounds',14:'trails',15:'goalexplosions',16:'playerbanners',18:'anthems',20:'avatarborders'};
    var category=categories[product.slot]||'',rarity=String(product.rarity||'').toLowerCase();
    var color=typeof OBJ_RARC!=='undefined'?OBJ_RARC[rarity]||'':'';
    var categoryName=category&&typeof objCatName==='function'?objCatName(category):'';
    var rarityName=rarity&&typeof objRarName==='function'?objRarName(rarity):rarity;
    var sub=esc(categoryName)+(rarityName?' &middot; <span'+(color?' style="color:'+color+'"':'')+'>'+esc(rarityName)+'</span>':'');
    return '<button type="button" class="tile obj-card replay-product-option'+(product.id===current?' objsel':'')+'" data-loadout-choice="'+product.id+'" aria-selected="'+(product.id===current)+'" title="'+esc(product.name)+'"'+(color?' style="border-color:'+color+'"':'')+'><span class="thumb replay-thumb">'+thumbnail(product.id)+'</span><span class="meta"><span class="t-name">'+esc(product.name)+'</span><span class="t-sub"><span class="t-cr">'+sub+'</span>'+(product.id>0?'<span class="t-id">ID : '+product.id+'</span>':'')+'</span></span></button>';
  }
  function loadoutHtml(index){
    var p=draft[index],team=p.loadouts[loadoutTeam];
    var header='<div class="modal-head"><div class="replay-dialog-heading"><div class="modal-title">Loadout</div><small>'+esc(p.name)+'</small></div><button class="btn replay-icon" data-replay-loadout-close aria-label="'+text('Close loadout','Fermer le loadout')+'"><i class="ph ph-x" aria-hidden="true"></i></button></div>';
    header+='<div class="replay-loadout-toolbar"><span class="replay-team-badge" data-team="'+p.team+'">'+(p.team===0?text('Blue team','Equipe bleue'):p.team===1?text('Orange team','Equipe orange'):text('Recorded loadout','Loadout enregistre'))+'</span><small>'+text('Loadout used in this replay','Loadout utilise dans ce replay')+'</small></div>';
    header+='<div class="replay-loadout-tabs" role="group" aria-label="'+text('Equipment category','Categorie d equipement')+'">'+[0,1].map(function(group){return '<button data-replay-group="'+group+'" aria-pressed="'+(loadoutGroup===group)+'">'+(group===0?text('Car','Voiture'):text('Profile & extras','Profil et accessoires'))+'</button>';}).join('')+'</div>';
    if(!team)return header;
    var car=['body','decal','wheels','boost','trail','goalExplosion'];
    var rows=equipment.filter(function(slot){return !!team.products[slot[0]]&&(car.indexOf(slot[0])>=0)===(loadoutGroup===0);}).map(function(slot){
      var item=team.products[slot[0]],product=productIndex[item.id],canPaint=item.paintMax>0&&product&&product.paintable;
      return '<div class="replay-equipment"><label>'+text(slot[2],slot[3])+'</label><div class="replay-equipment-controls"><button class="btn replay-product" data-replay-product="'+slot[0]+'" title="'+esc(productLabel(item.id))+'"><span class="replay-thumb">'+thumbnail(item.id)+'</span><span>'+esc(productLabel(item.id))+'</span><i class="ph ph-caret-down" aria-hidden="true"></i></button><button class="btn replay-paint" data-replay-paint="'+slot[0]+'" data-replay-unavailable="'+(canPaint?'0':'1')+'" title="'+(canPaint?text('Paint','Peinture'):text('Not paintable','Non peignable'))+'" aria-label="'+esc(text('Paint: ','Peinture : ')+paintLabel(item.paint))+'">'+paintChip(item.paint)+'<span>'+esc(paintLabel(item.paint))+'</span></button></div></div>';
    });
    if(loadoutGroup===0)rows=rows.concat(['primary','accent'].map(function(key){return '<div class="replay-equipment replay-finish"><label>'+ (key==='primary'?text('Primary finish','Finition principale'):text('Accent finish','Finition secondaire'))+'</label><button class="btn replay-product" data-replay-finish="'+key+'" data-replay-unavailable="'+(team.finishes?'0':'1')+'" title="'+esc(team.finishes?productLabel(team.finishes[key]):text('No finish recorded for this car','Aucune finition enregistree pour cette voiture'))+'"><span class="replay-thumb">'+(team.finishes?thumbnail(team.finishes[key]):'')+'</span><span>'+esc(team.finishes?productLabel(team.finishes[key]):text('Not recorded','Non enregistree'))+'</span><i class="ph ph-caret-down" aria-hidden="true"></i></button></div>';}));
    var area=document.querySelector('.win').getBoundingClientRect(),narrow=Math.min(880,area.width-32)<=540;
    var size=Math.max(2,Math.floor((area.height-254)/(narrow?66:72))*(narrow?1:2));
    var pages=Math.max(1,Math.ceil(rows.length/size));loadoutPage=Math.min(loadoutPage,pages-1);
    var paging=pages>1?pageButtons(loadoutPage,pages,'data-loadout-page'):'';
    return header+'<div class="replay-loadout-body">'+rows.slice(loadoutPage*size,(loadoutPage+1)*size).join('')+'</div><div class="replay-loadout-footer"><nav class="replay-pages" aria-label="'+text('Equipment','Equipement')+'">'+paging+'</nav><button class="btn" data-replay-loadout-close>'+text('Done','Terminer')+'</button></div>';
  }
  function closeLoadout(){
    var index=loadoutPlayer;loadoutPlayer=-1;render();var button=root.querySelector('[data-replay-loadout="'+index+'"]');if(button)button.focus();
  }
  function loadoutKey(e){
    if(!loadoutModal||modal)return;
    if(e.key==='Escape'){e.preventDefault();closeLoadout();return;}
    if(e.key==='Tab'){
      var nodes=Array.from(loadoutModal.querySelectorAll('button:not(:disabled)'));
      if(e.shiftKey&&e.target===nodes[0]){e.preventDefault();nodes[nodes.length-1].focus();}else if(!e.shiftKey&&e.target===nodes[nodes.length-1]){e.preventDefault();nodes[0].focus();}
    }
  }
  function renderLoadout(){
    if(loadoutPlayer<0||!state){if(loadoutModal){loadoutModal.remove();loadoutModal=null;document.removeEventListener('keydown',loadoutKey);}return;}
    if(!loadoutModal){
      loadoutModal=document.createElement('div');loadoutModal.className='modal-back show replay-modal';
      loadoutModal.addEventListener('click',function(e){if(e.target===loadoutModal)closeLoadout();else handleAction(e);});
      document.querySelector('.win').appendChild(loadoutModal);document.addEventListener('keydown',loadoutKey);
    }
    loadoutModal.innerHTML='<div class="modal-card replay-loadout-dialog" role="dialog" aria-modal="true" aria-label="'+esc('Loadout: '+draft[loadoutPlayer].name)+'">'+loadoutHtml(loadoutPlayer)+'</div>';
  }
  function playedTeam(player){
    if(!player.loadouts)return -1;
    if((player.team===0||player.team===1)&&player.loadouts[player.team])return player.team;
    var present=[0,1].filter(function(team){return !!player.loadouts[team];});
    return present.length===1?present[0]:-1;
  }
  async function refreshProducts(){
    var language=typeof lang!=='undefined'?lang:'fr';
    var r=await window.rippleCall('misc.replay.catalog');
    if(!plus)return;
    if(language!==(typeof lang!=='undefined'?lang:'fr'))return refreshProducts();
    products=r.products||[];productIndex={};products.forEach(function(p){productIndex[p.id]=p;});productLanguage=language;
  }
  async function openLoadout(index){
    if(busy||!plus||playedTeam(draft[index])<0)return;if(loadoutPlayer===index){loadoutPlayer=-1;render();return;}
    busy=true;availability();
    try{var language=typeof lang!=='undefined'?lang:'fr';if(!products.length||productLanguage!==language)await refreshProducts();if(!plus||!state)return;loadoutPlayer=index;loadoutGroup=0;loadoutPage=0;var player=draft[index];loadoutTeam=playedTeam(player);render();}
    catch(e){status(e.message||String(e),true);}finally{busy=false;availability();if(loadoutModal)loadoutModal.querySelector('[data-replay-group="'+loadoutGroup+'"]').focus();}
  }
  function loadoutPicker(slot,finish,paint){
    if(busy||!plus||loadoutPlayer<0)return;
    dismiss();var prior=document.activeElement,team=draft[loadoutPlayer].loadouts[loadoutTeam],item=team.products[slot],page=0,size=pickerSize(paint?'paint':'product');
    var category=finish?text('Paint finish','Finition'):text.apply(null,equipment.find(function(row){return row[0]===slot;}).slice(2));
    var heading=paint?text('Choose a paint','Choisir une peinture'):category;
    modal=document.createElement('div');modal.className='modal-back show replay-modal';
    modal.innerHTML='<div class="modal-card replay-picker" role="dialog" aria-modal="true" aria-label="'+heading+'"><div class="modal-head"><div class="replay-dialog-heading"><div class="modal-title">'+heading+'</div><small>'+esc(draft[loadoutPlayer].name)+' / '+(loadoutTeam===0?text('Blue','Bleue'):text('Orange','Orange'))+'</small></div><button class="btn replay-icon" data-loadout-close aria-label="'+text('Close','Fermer')+'"><i class="ph ph-x" aria-hidden="true"></i></button></div><div class="modal-body">'+(paint?'':'<div class="field"><i class="ph ph-magnifying-glass" aria-hidden="true"></i><input id="replayProductSearch" aria-label="'+text('Search products','Rechercher un produit')+'" placeholder="'+text('Search products','Rechercher un produit')+'" autocomplete="off"></div>')+'<div class="replay-options '+(paint?'replay-paints':'replay-product-options')+'"></div></div>'+pickerFooter()+'</div>';
    document.querySelector('.win').appendChild(modal);var options=modal.querySelector('.replay-options'),search=modal.querySelector('input');
    var cat=finish?7:equipment.find(function(row){return row[0]===slot;})[1];
    function list(){
      var current=paint?item.paint:finish?team.finishes[finish]:item.id,rows;
      if(paint)rows=paints.map(function(value,id){return {id:id,name:paintLabel(id)};}).filter(function(p){return p.id<=item.paintMax;});
      else {
        var query=search.value.trim().toLowerCase();rows=products.filter(function(p){return p.slot===cat&&(!query||String(p.name).toLowerCase().indexOf(query)>=0||String(p.id).indexOf(query)>=0);});
        if(!finish&&cat!==0&&!query)rows.unshift({id:0,name:text('None','Aucun')});
        if(!query)rows.sort(function(a,b){return (b.id===current?1:0)-(a.id===current?1:0);});
      }
      page=Math.min(page,Math.max(0,Math.ceil(rows.length/size)-1));
      options.innerHTML=rows.slice(page*size,(page+1)*size).map(function(p){return paint?'<button class="replay-paint-option" data-loadout-choice="'+p.id+'" title="'+esc(p.name)+'" aria-label="'+esc(p.name)+'" aria-pressed="'+(p.id===current)+'">'+paintChip(p.id)+'<span>'+esc(p.name)+'</span></button>':productCard(p,current);}).join('')||'<div class="replay-empty">'+text('No matching equipment','Aucun equipement correspondant')+'</div>';
      updatePickerPage(modal,rows.length,page,size);
    }
    modalRelabel=function(){
      var heading=paint?text('Choose a paint','Choisir une peinture'):finish?text('Paint finish','Finition'):text.apply(null,equipment.find(function(row){return row[0]===slot;}).slice(2));
      modal.querySelector('[role="dialog"]').setAttribute('aria-label',heading);
      modal.querySelector('.modal-title').textContent=heading;
      modal.querySelector('.replay-dialog-heading small').textContent=draft[loadoutPlayer].name+' / '+(loadoutTeam===0?text('Blue','Bleue'):text('Orange','Orange'));
      modal.querySelector('[data-loadout-close]').setAttribute('aria-label',text('Close','Fermer'));
      if(search){search.placeholder=text('Search products','Rechercher un produit');search.setAttribute('aria-label',search.placeholder);}
      modal.querySelector('.replay-pages').setAttribute('aria-label',text('Results','Resultats'));
      list();
    };
    function resize(){size=pickerSize(paint?'paint':'product');list();}
    function close(){window.removeEventListener('resize',resize);document.removeEventListener('keydown',key);modalDismiss=null;dismiss();if(prior&&prior.isConnected)prior.focus();else if(loadoutModal)loadoutModal.querySelector('button:not(:disabled)').focus();}
    modalDismiss=close;
    function key(e){if(e.key==='Escape'){e.preventDefault();close();return;}trapPicker(e,options,search);}
    modal.addEventListener('click',function(e){var choice=e.target.closest('[data-loadout-choice]'),pager=e.target.closest('[data-picker-page]');if(choice){var id=+choice.dataset.loadoutChoice;if(paint)item.paint=id;else if(finish)team.finishes[finish]=id;else if(item.id!==id){item.id=id;item.paint=0;}close();render();var selector=paint?'[data-replay-paint="'+slot+'"]':finish?'[data-replay-finish="'+finish+'"]':'[data-replay-product="'+slot+'"]';var button=loadoutModal&&loadoutModal.querySelector(selector);if(button)button.focus();}else if(pager&&!pager.disabled){page+=+pager.dataset.pickerPage;list();focusPickerPage(pager.dataset.pickerPage);}else if(e.target===modal||e.target.closest('[data-loadout-close]'))close();});
    if(search)search.addEventListener('input',function(){page=0;list();});window.addEventListener('resize',resize);document.addEventListener('keydown',key);list();(search||options.querySelector('button')).focus();
  }
  window.replayRefresh=function(){
    if(refreshing)return refreshing;
    refreshing=window.rippleCall('misc.replay.state').then(function(r){
      plus=!!r.plus;titles=r.titles||{};
      var tab=document.querySelector('.tab-strip button[data-sub="m-replay"]');if(tab)tab.classList.toggle('locked',!plus);
      if(!plus){dismiss();accept(null);return;}
      if(!state&&r.session)accept(r);else render();
      if(!state)return refreshLibrary(library.page);
    }).catch(function(){plus=false;dismiss();accept(null);var tab=document.querySelector('.tab-strip button[data-sub="m-replay"]');if(tab)tab.classList.add('locked');}).finally(function(){refreshing=null;});
    return refreshing;
  };
  function handleAction(e){
    var page=e.target.closest('[data-replay-page]');if(page&&!page.disabled){playerPage+=+page.dataset.replayPage;renderRows();availability();var next=byId('replayPages').querySelector('[data-replay-page="'+page.dataset.replayPage+'"]');if(next&&!next.disabled)next.focus();return;}
    var title=e.target.closest('[data-replay-title]');if(title){picker(+title.dataset.replayTitle);return;}
    var reset=e.target.closest('[data-replay-reset]');if(reset){var index=+reset.dataset.replayReset;draft[index]=clone(state.players[index]);render();return;}
    var loadout=e.target.closest('[data-replay-loadout]');if(loadout){openLoadout(+loadout.dataset.replayLoadout);return;}
    if(busy||libraryLoading||!plus)return;
    var entry=e.target.closest('[data-replay-entry]');if(entry&&!entry.disabled){open({entry:entry.dataset.replayEntry});return;}
    var libraryPage=e.target.closest('[data-library-page]');if(libraryPage&&!libraryPage.disabled){refreshLibrary(library.page+(+libraryPage.dataset.libraryPage));return;}
    var loadoutPager=e.target.closest('[data-loadout-page]');if(loadoutPager&&!loadoutPager.disabled){loadoutPage+=+loadoutPager.dataset.loadoutPage;render();var next=loadoutModal.querySelector('[data-loadout-page]:not(:disabled)');if(next)next.focus();return;}
    if(e.target.closest('[data-replay-loadout-close]')){closeLoadout();return;}
    var group=e.target.closest('[data-replay-group]');if(group){loadoutGroup=+group.dataset.replayGroup;loadoutPage=0;render();loadoutModal.querySelector('[data-replay-group="'+loadoutGroup+'"]').focus();return;}
    var product=e.target.closest('[data-replay-product]'),paint=e.target.closest('[data-replay-paint]'),finish=e.target.closest('[data-replay-finish]');
    if(product||paint||finish){if((product||paint||finish).disabled)return;loadoutPicker(product?product.dataset.replayProduct:paint?paint.dataset.replayPaint:null,finish?finish.dataset.replayFinish:null,!!paint);return;}

  }
  root.addEventListener('click',handleAction);
  root.addEventListener('input',function(e){
    var input=e.target.closest('[data-replay-name]');if(!input||!state)return;
    var index=+input.dataset.replayName;draft[index].name=input.value;
    var row=input.closest('.replay-row');if(row)row.classList.toggle('is-changed',changed(draft[index],index));availability();
  });
  root.addEventListener('change',function(e){
    var input=e.target.closest('[data-replay-name]');if(!input||!state)return;
    var index=+input.dataset.replayName;draft[index].name=nameOf(draft[index],index);input.value=draft[index].name;
  });
  root.addEventListener('keydown',function(e){
    var input=e.target.closest('[data-replay-name]');if(!input||!state)return;
    if(e.key==='Enter'){e.preventDefault();input.blur();}
    else if(e.key==='Escape'){e.preventDefault();var index=+input.dataset.replayName;draft[index].name=state.players[index].name;input.value=draft[index].name;input.dispatchEvent(new Event('input',{bubbles:true}));input.blur();}
  });
  byId('replayLibraryRows').addEventListener('scroll',function(){if(!state&&!libraryLoading&&this.getClientRects().length)libraryScroll=this.scrollTop;});
  document.addEventListener('error',function(e){if(e.target.tagName==='IMG'&&e.target.closest('.replay-thumb')){e.target.remove();}},true);
  byId('replayLibraryRefresh').addEventListener('click',function(){refreshLibrary(library.page,true);});
  byId('replayOpen').addEventListener('click',function(){open();});
  byId('replayDrop').addEventListener('click',function(){open();});
  byId('replaySaveCopy').addEventListener('click',function(){save(false);});
  byId('replayReplace').addEventListener('click',function(){save(true);});
  byId('replayResetAll').addEventListener('click',function(){draft=state.players.map(clone);render();status('');});
  byId('replayClose').addEventListener('click',async function(){if(busy)return;busy=true;availability();try{if(!await discard())return;await window.rippleCall('misc.replay.close');accept(null);status('');}catch(e){status(e.message,true);}finally{busy=false;availability();if(!state)refreshLibrary(library.page);}});
  document.addEventListener('ripple:drop.over',function(e){var d=e.detail||{},target=document.elementFromPoint(d.x||0,d.y||0);byId('replayDrop').classList.toggle('dz-hot',!!d.active&&!!target&&root.contains(target));});
  document.addEventListener('ripple:drop.file',function(e){var d=e.detail||{},target=document.elementFromPoint(d.x||0,d.y||0);if(target&&root.contains(target))open({path:d.path});});
  document.addEventListener('drop',function(e){
    if(!e.target.closest('[data-drop="replay"]'))return;
    e.preventDefault();var files=e.dataTransfer&&e.dataTransfer.files;if(!files||files.length!==1||busy||!plus)return;
    var file=files[0];if(!/\.replay$/i.test(file.name)||file.size>32*1024*1024){status(text('Select a .replay file smaller than 32 MB.','Selectionne un fichier .replay de moins de 32 Mo.'),true);return;}
    var reader=new FileReader();reader.onload=function(){open({name:file.name,data:String(reader.result).split(',')[1]});};reader.onerror=function(){status(text('Could not read the replay.','Lecture du replay impossible.'),true);};reader.readAsDataURL(file);
  });
  var languageRevision=0;
  document.addEventListener('ripple:language.changed',function(){
    var revision=++languageRevision;
    setTimeout(function(){
      if(revision!==languageRevision)return;
      render();if(modalRelabel)modalRelabel();
      var language=typeof lang!=='undefined'?lang:'fr';
      if(!products.length||productLanguage===language||!plus)return;
      refreshProducts().then(function(){
        if(revision!==languageRevision||!plus)return;
        renderLoadout();if(modalRelabel)modalRelabel();availability();
      }).catch(function(e){status(e.message||String(e),true);});
    },0);
  });
  document.addEventListener('click',function(e){if(e.target.closest('[data-sub="m-replay"]'))window.replayRefresh();});
  if(window.ResizeObserver)new ResizeObserver(function(){cancelAnimationFrame(resizeFrame);resizeFrame=requestAnimationFrame(fitPlayers);}).observe(byId('replayRows'));
  window.addEventListener('resize',function(){cancelAnimationFrame(resizeFrame);resizeFrame=requestAnimationFrame(fitPlayers);if(loadoutModal){var focused=loadoutModal.contains(document.activeElement);renderLoadout();availability();if(focused)loadoutModal.querySelector('[data-replay-group="'+loadoutGroup+'"]').focus();}});
  render();
  if(typeof window.rippleCall==='function')window.replayRefresh();
})();
