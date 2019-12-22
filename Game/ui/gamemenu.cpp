#include "gamemenu.h"

#include <Tempest/Painter>
#include <Tempest/Log>
#include <Tempest/TextCodec>

#include "utils/gthfont.h"
#include "world/world.h"
#include "ui/menuroot.h"
#include "utils/fileutil.h"
#include "game/serialize.h"
#include "game/savegameheader.h"
#include "gothic.h"
#include "resources.h"

using namespace Tempest;

static const float scriptDiv=8192.0f;

GameMenu::GameMenu(MenuRoot &owner, Daedalus::DaedalusVM &vm, Gothic &gothic, const char* menuSection)
  :gothic(gothic), owner(owner), vm(vm) {
  timer.timeout.bind(this,&GameMenu::onTick);
  timer.start(100);

  textBuf.reserve(64);

  Daedalus::DATFile& dat=vm.getDATFile();
  vm.initializeInstance(menu,
                        dat.getSymbolIndexByName(menuSection),
                        Daedalus::IC_Menu);
  back = Resources::loadTexture(menu.backPic.c_str());

  initItems();
  if(menu.flags & Daedalus::GEngineClasses::C_Menu::MENU_SHOW_INFO) {
    float infoX = 1000.0f/scriptDiv;
    float infoY = 7500.0f/scriptDiv;

    // There could be script-defined values
    if(dat.hasSymbolName("MENU_INFO_X") && dat.hasSymbolName("MENU_INFO_X")) {
      Daedalus::PARSymbol& symX = dat.getSymbolByName("MENU_INFO_X");
      Daedalus::PARSymbol& symY = dat.getSymbolByName("MENU_INFO_Y");

      infoX = symX.getInt()/scriptDiv;
      infoY = symY.getInt()/scriptDiv;
      }
    setPosition(int(infoX*w()),int(infoY*h()));
    }

  setSelection(gothic.isInGame() ? menu.defaultInGame : menu.defaultOutGame);
  initValues();

  gothic.pushPause();
  }

GameMenu::~GameMenu() {
  for(int i=0;i<Daedalus::GEngineClasses::MenuConstants::MAX_ITEMS;++i)
    vm.clearReferences(hItems[i].handle);
  vm.clearReferences(menu);

  gothic.flushSettings();
  gothic.popPause();
  }

void GameMenu::initItems() {
  for(int i=0;i<Daedalus::GEngineClasses::MenuConstants::MAX_ITEMS;++i){
    if(menu.items[i].empty())
      continue;

    hItems[i].name = menu.items[i].c_str();
    vm.initializeInstance(hItems[i].handle,
                          vm.getDATFile().getSymbolIndexByName(hItems[i].name.c_str()),
                          Daedalus::IC_MenuItem);
    hItems[i].img = Resources::loadTexture(hItems[i].handle.backPic.c_str());
    updateItem(hItems[i]);
    }
  }

void GameMenu::paintEvent(PaintEvent &e) {
  Painter p(e);
  if(back) {
    p.setBrush(*back);
    p.drawRect(0,0,w(),h(),
               0,0,back->w(),back->h());
    }

  for(auto& hItem:hItems) {
    if(!hItem.visible)
      continue;
    Daedalus::GEngineClasses::C_Menu_Item&        item = hItem.handle;
    Daedalus::GEngineClasses::C_Menu_Item::EFlags flags=Daedalus::GEngineClasses::C_Menu_Item::EFlags(item.flags);
    getText(hItem,textBuf);

    int x = int(w()*item.posx/scriptDiv);
    int y = int(h()*item.posy/scriptDiv);
    int imgX = 0, imgW=0;

    if(hItem.img && !hItem.img->isEmpty()) {
      int32_t dimx = 8192;
      int32_t dimy = 750;

      if(item.dimx!=-1) dimx = item.dimx;
      if(item.dimy!=-1) dimy = item.dimy;

      const int szX = int(w()*dimx/scriptDiv);
      const int szY = int(h()*dimy/scriptDiv);
      p.setBrush(*hItem.img);
      p.drawRect(/*(w()-szX)/2*/x,y,szX,szY,
                 0,0,hItem.img->w(),hItem.img->h());

      imgX = x;
      imgW = szX;
      }

    auto& fnt = getTextFont(hItem);

    if(flags & Daedalus::GEngineClasses::C_Menu_Item::IT_TXT_CENTER){
      Size sz = fnt.textSize(textBuf.data());
      if(hItem.img && !hItem.img->isEmpty()) {
        x = imgX+(imgW-sz.w)/2;
        }
      else if(item.dimx!=-1) {
        const int szX = int(w()*item.dimx/scriptDiv);
        // const int szY = int(h()*item.dimy/scriptDiv);
        x = x+(szX-sz.w)/2;
        } else {
        x = (w()-sz.w)/2;
        }
      //y += sz.h/2;
      }

    fnt.drawText(p,x,y+fnt.pixelSize(),textBuf.data());
    }

  if(auto sel=selectedItem()) {
    auto&                                  fnt  = Resources::font();
    Daedalus::GEngineClasses::C_Menu_Item& item = sel->handle;
    if(item.text->size()>1) {
      const char* txt = item.text[1].c_str();
      int tw = fnt.textSize(txt).w;
      fnt.drawText(p,(w()-tw)/2,h()-12,txt);
      }
    }
  }

void GameMenu::resizeEvent(SizeEvent &) {
  onTick();
  }

void GameMenu::onMove(int dy) {
  gothic.emitGlobalSound(gothic.loadSoundFx("MENU_BROWSE"));
  setSelection(int(curItem)+dy, dy>0 ? 1 : -1);
  if(auto s = selectedItem())
    updateSavThumb(*s);
  update();
  }

void GameMenu::onSelect() {
  if(auto sel=selectedItem()){
    gothic.emitGlobalSound(gothic.loadSoundFx("MENU_SELECT"));
    exec(*sel);
    }
  }

void GameMenu::onTick() {
  update();
  gothic.setMusic(GameMusic::SysMenu);

  const float fx = 640.0f;
  const float fy = 480.0f;

  if(menu.flags & Daedalus::GEngineClasses::C_Menu::MENU_DONTSCALE_DIM)
    resize(int(menu.dimx/scriptDiv*fx),int(menu.dimy/scriptDiv*fy));

  if(menu.flags & Daedalus::GEngineClasses::C_Menu::MENU_ALIGN_CENTER) {
    setPosition((owner.w()-w())/2, (owner.h()-h())/2);
    }
  else if(menu.flags & Daedalus::GEngineClasses::C_Menu::MENU_DONTSCALE_DIM)
    setPosition(int(menu.posx/scriptDiv*fx), int(menu.posy/scriptDiv*fy));
  }

GameMenu::Item *GameMenu::selectedItem() {
  if(curItem<Daedalus::GEngineClasses::MenuConstants::MAX_ITEMS)
    return &hItems[curItem];
  return nullptr;
  }

GameMenu::Item *GameMenu::selectedNextItem(Item *it) {
  uint32_t cur=curItem+1;
  for(uint32_t i=0;i<Daedalus::GEngineClasses::MenuConstants::MAX_ITEMS;++i)
    if(&hItems[i]==it) {
      cur=i+1;
      break;
      }

  for(int i=0;i<Daedalus::GEngineClasses::MenuConstants::MAX_ITEMS;++i,cur++) {
    cur%=Daedalus::GEngineClasses::MenuConstants::MAX_ITEMS;

    auto& it=hItems[cur].handle;
    if(isEnabled(it))
      return &hItems[cur];
    }
  return nullptr;
  }

void GameMenu::setSelection(int desired,int seek) {
  uint32_t cur=uint32_t(desired);
  for(int i=0; i<Daedalus::GEngineClasses::MenuConstants::MAX_ITEMS; ++i,cur+=uint32_t(seek)) {
    cur%=Daedalus::GEngineClasses::MenuConstants::MAX_ITEMS;

    auto& it=hItems[cur].handle;
    if((it.flags&Daedalus::GEngineClasses::C_Menu_Item::EFlags::IT_SELECTABLE) && isEnabled(it)){
      curItem=cur;
      return;
      }
    }
  curItem=uint32_t(-1);
  }

void GameMenu::getText(const Item& it, std::vector<char> &out) {
  if(out.size()==0)
    out.resize(1);
  out[0]='\0';

  const auto& src = it.handle.text[0];
  if(it.handle.type==Daedalus::GEngineClasses::C_Menu_Item::MENU_ITEM_TEXT) {
    size_t size = std::strlen(src.c_str());
    out.resize(size+1);
    std::memcpy(out.data(),src.c_str(),size+1);
    return;
    }

  if(it.handle.type==Daedalus::GEngineClasses::C_Menu_Item::MENU_ITEM_CHOICEBOX) {
    strEnum(src.c_str(),it.value,out);
    return;
    }
  }

const GthFont &GameMenu::getTextFont(const GameMenu::Item &it) {
  if(!isEnabled(it.handle))
    return Resources::font(it.handle.fontName.c_str(),Resources::FontType::Disabled);
  if(&it==selectedItem())
    return Resources::font(it.handle.fontName.c_str(),Resources::FontType::Hi);
  return Resources::font(it.handle.fontName.c_str());
  }

bool GameMenu::isEnabled(const Daedalus::GEngineClasses::C_Menu_Item &item) {
  if((item.flags & Daedalus::GEngineClasses::C_Menu_Item::IT_ONLY_IN_GAME) && !gothic.isInGame())
    return false;
  if((item.flags & Daedalus::GEngineClasses::C_Menu_Item::IT_ONLY_OUT_GAME) && gothic.isInGame())
    return false;
  return true;
  }

void GameMenu::exec(Item &p) {
  auto* it = &p;
  while(it!=nullptr){
    if(it==&p)
      execSingle(*it); else
      execChgOption(*it);
    if(it->handle.flags & Daedalus::GEngineClasses::C_Menu_Item::IT_EFFECTS_NEXT) {
      auto next=selectedNextItem(it);
      if(next!=&p)
        it=next;
      } else {
      it=nullptr;
      }
    }

  if(closeFlag)
    owner.closeAll();
  else if(exitFlag)
    owner.popMenu();
  }

void GameMenu::execSingle(Item &it) {
  using namespace Daedalus::GEngineClasses::MenuConstants;

  auto& item          = it.handle;
  auto& onSelAction   = item.onSelAction;
  auto& onSelAction_S = item.onSelAction_S;
  auto& onEventAction = item.onEventAction;

  for(size_t i=0;i<Daedalus::GEngineClasses::MenuConstants::MAX_SEL_ACTIONS;++i) {
    auto action = onSelAction[i];
    switch(action) {
      case SEL_ACTION_UNDEF:
        break;
      case SEL_ACTION_BACK:
        gothic.emitGlobalSound(gothic.loadSoundFx("MENU_ESC"));
        exitFlag = true;
        break;
      case SEL_ACTION_STARTMENU:
        if(vm.getDATFile().hasSymbolName(onSelAction_S[i].c_str()))
          owner.pushMenu(new GameMenu(owner,vm,gothic,onSelAction_S[i].c_str()));
        break;
      case SEL_ACTION_STARTITEM:
        break;
      case SEL_ACTION_CLOSE:
        gothic.emitGlobalSound(gothic.loadSoundFx("MENU_ESC"));
        closeFlag = true;
        if(onSelAction_S[i]=="NEW_GAME")
          gothic.onStartGame(gothic.defaultWorld());
        else if(onSelAction_S[i]=="LEAVE_GAME")
          Tempest::SystemApi::exit();
        else if(onSelAction_S[i]=="SAVEGAME_SAVE")
          execSaveGame(it);
        else if(onSelAction_S[i]=="SAVEGAME_LOAD")
          execLoadGame(it);
        break;
      case SEL_ACTION_CONCOMMANDS:
        // unknown
        break;
      case SEL_ACTION_PLAY_SOUND:
        gothic.emitGlobalSound(gothic.loadSoundFx(onSelAction_S[i].c_str()));
        break;
      case SEL_ACTION_EXECCOMMANDS:
        break;
      }
    }

  if(onEventAction[SEL_EVENT_EXECUTE]>0){
    vm.runFunctionBySymIndex(size_t(onEventAction[SEL_EVENT_EXECUTE]),true);
    }

  execChgOption(it);
  }

void GameMenu::execChgOption(Item &item) {
  auto& sec = item.handle.onChgSetOptionSection;
  auto& opt = item.handle.onChgSetOption;
  if(sec.empty() || opt.empty())
    return;

  updateItem(item);
  item.value += 1; // next value

  size_t cnt = strEnumSize(item.handle.text[0].c_str());
  if(cnt>0)
    item.value%=cnt; else
    item.value =0;
  gothic.settingsSetI(sec.c_str(), opt.c_str(), item.value);
  }

void GameMenu::execSaveGame(GameMenu::Item &item) {
  const size_t id = saveSlotId(item);
  if(id==size_t(-1))
    return;

  char fname[64]={};
  std::snprintf(fname,sizeof(fname)-1,"save_slot_%d.sav",id);
  gothic.save(fname);
  }

void GameMenu::execLoadGame(GameMenu::Item &item) {
  const size_t id = saveSlotId(item);
  if(id==size_t(-1))
    return;

  char fname[64]={};
  std::snprintf(fname,sizeof(fname)-1,"save_slot_%d.sav",id);
  gothic.load(fname);
  }

void GameMenu::updateItem(GameMenu::Item &item) {
  auto& it   = item.handle;
  item.value = gothic.settingsGetI(it.onChgSetOptionSection.c_str(), it.onChgSetOption.c_str());
  }

void GameMenu::updateSavThumb(GameMenu::Item &sel) {
  if(sel.handle.onSelAction_S[0]!="SAVEGAME_LOAD" &&
     sel.handle.onSelAction_S[1]!="SAVEGAME_SAVE")
    return;

  if(!implUpdateSavThumb(sel)) {
    set("MENUITEM_LOADSAVE_THUMBPIC",static_cast<Texture2d*>(nullptr));
    set("MENUITEM_LOADSAVE_LEVELNAME_VALUE","");
    set("MENUITEM_LOADSAVE_DATETIME_VALUE", "");
    set("MENUITEM_LOADSAVE_GAMETIME_VALUE", "");
    set("MENUITEM_LOADSAVE_PLAYTIME_VALUE", "");
    }
  }

bool GameMenu::implUpdateSavThumb(GameMenu::Item& sel) {
  const size_t id = saveSlotId(sel);
  if(id==size_t(-1))
    return false;

  char fname[64]={};
  std::snprintf(fname,sizeof(fname)-1,"save_slot_%d.sav",id);

  if(!FileUtil::exists(TextCodec::toUtf16(fname)))
    return false;

  SaveGameHeader hdr;
  try {
    RFile     fin(fname);
    Serialize reader(fin);
    reader.read(hdr);
    }
  catch(std::bad_alloc&) {
    return false;
    }
  catch(std::system_error& e) {
    Log::d(e.what());
    return false;
    }

  char form[64]={};
  savThumb = Resources::loadTexture(hdr.priview);

  set("MENUITEM_LOADSAVE_THUMBPIC",       &savThumb);
  set("MENUITEM_LOADSAVE_LEVELNAME_VALUE",hdr.world.c_str());

  std::snprintf(form,sizeof(form),"%d:%02d",int(hdr.pcTime.hour()),int(hdr.pcTime.minute()));
  set("MENUITEM_LOADSAVE_DATETIME_VALUE", form);

  std::snprintf(form,sizeof(form),"%d - %d:%02d",int(hdr.wrldTime.day()),int(hdr.wrldTime.hour()),int(hdr.wrldTime.minute()));
  set("MENUITEM_LOADSAVE_GAMETIME_VALUE", form);

  set("MENUITEM_LOADSAVE_PLAYTIME_VALUE", "");
  return true;
  }

size_t GameMenu::saveSlotId(const GameMenu::Item &sel) {
  const char* prefix[2] = {"MENUITEM_LOAD_SLOT", "MENUITEM_SAVE_SLOT"};
  for(auto p:prefix) {
    if(sel.name.find(p)==0){
      size_t off=std::strlen(p);
      size_t id=0;
      for(size_t i=off;i<sel.name.size();++i){
        if('0'<=sel.name[i] && sel.name[i]<='9') {
          id=id*10+size_t(sel.name[i]-'0');
          } else {
          return size_t(-1);
          }
        }
      return id;
      }
    }
  return size_t(-1);
  }

const char *GameMenu::strEnum(const char *en, int id, std::vector<char> &out) {
  int num=0;

  size_t i=0;
  for(size_t r=0;en[r];++r)
    if(en[r]=='#'){
      i=r+1;
      break;
      }

  for(;en[i];++i){
    size_t b=i;
    for(size_t r=i;en[r];++r,++i)
      if(en[r]=='|')
        break;

    if(id==num) {
      size_t sz=i-b;
      out.resize(sz+1);
      std::memcpy(&out[0],&en[b],sz);
      out[sz]='\0';
      return textBuf.data();
      }
    ++num;
    }
  for(size_t i=0;en[i];++i){
    if(en[i]=='#' || en[i]=='|'){
      out.resize(i+1);
      std::memcpy(&out[0],en,i);
      out[i]='\0';
      return out.data();
      }
    }
  return "";
  }

size_t GameMenu::strEnumSize(const char *en) {
  if(en==nullptr)
    return 0;
  size_t cnt = 0;
  for(size_t i=0;en[i];++i) {
    if(en[i]=='#' || en[i]=='|') {
      cnt += en[i]=='|' ? 2 : 1;
      i++;
      for(;en[i];++i) {
        if(en[i]=='|')
          cnt++;
        }
      }
    }
  return cnt;
  }

void GameMenu::set(const char *item, const Texture2d *value) {
  for(auto& i:hItems)
    if(i.name==item) {
      i.img = value;
      return;
      }
  }

void GameMenu::set(const char *item, const uint32_t value) {
  char buf[16]={};
  std::snprintf(buf,sizeof(buf),"%u",value);
  set(item,buf);
  }

void GameMenu::set(const char *item, const int32_t value) {
  char buf[16]={};
  std::snprintf(buf,sizeof(buf),"%d",value);
  set(item,buf);
  }

void GameMenu::set(const char *item, const int32_t value, const char *post) {
  char buf[32]={};
  std::snprintf(buf,sizeof(buf),"%d%s",value,post);
  set(item,buf);
  }

void GameMenu::set(const char *item, const int32_t value, const int32_t max) {
  char buf[32]={};
  std::snprintf(buf,sizeof(buf),"%d/%d",value,max);
  set(item,buf);
  }

void GameMenu::set(const char *item, const char *value) {
  for(auto& i:hItems)
    if(i.name==item) {
      i.handle.text[0]=value;
      return;
      }
  }

void GameMenu::initValues() {
  gtime time;
  if(auto w = gothic.world())
    time = w->time();

  set("MENU_ITEM_PLAYERGUILD","Debugger");
  set("MENU_ITEM_LEVEL","0");
  for(auto& i:hItems) {
    if(i.name=="MENU_ITEM_CONTENT_VIEWER") {
      i.visible=false;
      }
    if(i.name=="MENU_ITEM_DAY") {
      i.handle.text[0] = Daedalus::ZString::toStr(time.day());
      }
    if(i.name=="MENU_ITEM_TIME") {
      char form[64]={};
      std::snprintf(form,sizeof(form),"%d:%02d",int(time.hour()),int(time.minute()));
      i.handle.text[0] = form;
      }
    }

  if(auto s = selectedItem())
    updateSavThumb(*s);
  }

void GameMenu::setPlayer(const Npc &pl) {
  auto world = gothic.world();
  if(world==nullptr)
    return;

  auto& gilds = world->getSymbol("TXT_GUILDS");
  auto& tal   = world->getSymbol("TXT_TALENTS");
  auto& talV  = world->getSymbol("TXT_TALENTS_SKILLS");

  set("MENU_ITEM_PLAYERGUILD",gilds.getString(pl.guild()).c_str());

  set("MENU_ITEM_LEVEL",      pl.level());
  set("MENU_ITEM_EXP",        pl.experience());
  set("MENU_ITEM_LEVEL_NEXT", pl.experienceNext());
  set("MENU_ITEM_LEARN",      pl.learningPoints());

  set("MENU_ITEM_ATTRIBUTE_1", pl.attribute(Npc::ATR_STRENGTH));
  set("MENU_ITEM_ATTRIBUTE_2", pl.attribute(Npc::ATR_DEXTERITY));
  set("MENU_ITEM_ATTRIBUTE_3", pl.attribute(Npc::ATR_MANA),      pl.attribute(Npc::ATR_MANAMAX));
  set("MENU_ITEM_ATTRIBUTE_4", pl.attribute(Npc::ATR_HITPOINTS), pl.attribute(Npc::ATR_HITPOINTSMAX));

  set("MENU_ITEM_ARMOR_1", pl.protection(Npc::PROT_EDGE));
  set("MENU_ITEM_ARMOR_2", pl.protection(Npc::PROT_POINT)); // not sure about it
  set("MENU_ITEM_ARMOR_3", pl.protection(Npc::PROT_FIRE));
  set("MENU_ITEM_ARMOR_4", pl.protection(Npc::PROT_MAGIC));

  const int talentMax = gothic.version().game==2 ? Npc::TALENT_MAX_G2 : Npc::TALENT_MAX_G1;
  for(int i=0;i<talentMax;++i){
    auto& str = tal.getString(size_t(i));
    if(str.empty())
      continue;

    char buf[64]={};
    std::snprintf(buf,sizeof(buf),"MENU_ITEM_TALENT_%d_TITLE",i);
    set(buf,str.c_str());

    const int sk=pl.talentSkill(Npc::Talent(i));
    std::snprintf(buf,sizeof(buf),"MENU_ITEM_TALENT_%d_SKILL",i);
    set(buf,strEnum(talV.getString(size_t(i)).c_str(),sk,textBuf));

    const int val=pl.hitChanse(Npc::Talent(i));
    std::snprintf(buf,sizeof(buf),"MENU_ITEM_TALENT_%d",i);
    set(buf,val,"%");
    }
  }
