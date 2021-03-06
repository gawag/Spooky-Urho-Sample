#include "misc.h"
#include "gs_playing.h"

#include <map>
#include <Urho3D/Graphics/BillboardSet.h>
#include <Urho3D/ThirdParty/PugiXml/pugixml.hpp>

#include "gs_pause.h"
#include "gs_level_end.h"
#include "gs_death.h"

using namespace std;
using namespace Urho3D;

// it may be better to do file loading with the proper Urho functions which search all registered paths
level::level(std::string level_filename)
{
    if(level_filename.substr(level_filename.size()-4)==".lua")
    {
        load_lua_level(level_filename);
        return;
    }

    pugi::xml_document doc;
    pugi::xml_parse_result result=doc.load_file(("Data/"+level_filename).c_str());
    if(!result)
    {
        std::cout<<"XML parsed with errors, attr value: ["<<doc.child("node").attribute("attr").value()<<"]\n";
        std::cout<<"Error description: "<<result.description()<<"\n";
        std::cout<<"Error offset: "<<result.offset<<" (error at [..."<<(result.offset)<<"]\n\n";
    }

    for(auto& c:doc.children())
    {
        for(pugi::xml_attribute& attr:c.attributes())
            if(std::string(attr.name())=="skybox")
                skybox_material=attr.value();
            else if(std::string(attr.name())=="level_min_height")
                level_min_height=std::stof(std::string(attr.value()));
            else if(std::string(attr.name())=="gravity")
                gravity=std::stof(std::string(attr.value()));

        for(pugi::xml_node& child:c.children())
        {
            std::string name(child.name());
            if(name=="static_model")
            {
                float pos_x=0;
                float pos_y=0;
                float pos_z=0;
                float scale=1.0;
                String name;

                for(pugi::xml_attribute& attr:child.attributes())
                {
                    if(std::string(attr.name())=="name")
                        name=attr.value();
                    else if(std::string(attr.name())=="pos_x")
                        pos_x=std::stof(std::string(attr.value()));
                    else if(std::string(attr.name())=="pos_y")
                        pos_y=std::stof(std::string(attr.value()));
                    else if(std::string(attr.name())=="pos_z")
                        pos_z=std::stof(std::string(attr.value()));
                    else if(std::string(attr.name())=="scale")
                        scale=std::stof(std::string(attr.value()));
                }
                if(name.Length())
                    static_models.emplace_back(name,Vector3(pos_x,pos_y,pos_z),scale);
            }
            else if(name=="flag")
            {
                float pos_x=0;
                float pos_y=0;
                float pos_z=0;

                for(pugi::xml_attribute& attr:child.attributes())
                {
                    if(std::string(attr.name())=="pos_x")
                        pos_x=std::stof(std::string(attr.value()));
                    else if(std::string(attr.name())=="pos_y")
                        pos_y=std::stof(std::string(attr.value()));
                    else if(std::string(attr.name())=="pos_z")
                        pos_z=std::stof(std::string(attr.value()));
                }

                flag_positions.emplace_back(pos_x,pos_y,pos_z);
            }
            else if(name=="torch")
            {
                float pos_x=0;
                float pos_y=0;
                float pos_z=0;

                for(pugi::xml_attribute& attr:child.attributes())
                {
                    if(std::string(attr.name())=="pos_x")
                        pos_x=std::stof(std::string(attr.value()));
                    else if(std::string(attr.name())=="pos_y")
                        pos_y=std::stof(std::string(attr.value()));
                    else if(std::string(attr.name())=="pos_z")
                        pos_z=std::stof(std::string(attr.value()));
                }

                torch_positions.emplace_back(pos_x,pos_y,pos_z);
            }
            else if(name=="player")
            {
                for(pugi::xml_attribute& attr:child.attributes())
                {
                    if(std::string(attr.name())=="pos_x")
                        player_pos.x_=std::stof(std::string(attr.value()));
                    else if(std::string(attr.name())=="pos_y")
                        player_pos.y_=std::stof(std::string(attr.value()));
                    else if(std::string(attr.name())=="pos_z")
                        player_pos.z_=std::stof(std::string(attr.value()));
                }
            }
            else if(name=="sound")
            {
                for(pugi::xml_attribute& attr:child.attributes())
                    if(std::string(attr.name())=="name")
                        sound_name=attr.value();
            }
        }
    }

    // generate random map by combining world parts
    {
        // place a starting world part
        {
            world_part wp("mineshaft_straight",Vector3(0,50,0));
            world_parts.push_back(wp);
        }

        // TODO add adjustable room probability.
        std::vector<String> world_part_list{"mineshaft_straight","mineshaft_straight","mineshaft_curve_90","mineshaft_cross","mineshaft_cross","mineshaft_ramp"};

        // place 50 other parts
        while(world_parts.size()<50||flag_positions.size()==0)
        {
            world_part* last_wp=&world_parts[Random(0,world_parts.size())];
            world_part wp(world_part_list[Random(0,world_part_list.size())]);
            String from_dock=wp.get_free_dock_name();
            String to_dock=last_wp->get_free_dock_name();
            if(!(from_dock.Length()&&to_dock.Length()))
                continue;
            if(!wp.move_to_docking_point(from_dock,*last_wp,to_dock))
                continue;

            if(wp.spawn_points.size())
            {
                float r=Random();
                if(r<0.2)
                {
                    auto p=wp.node->node->GetChild(wp.spawn_points[0],true)->GetWorldPosition();
                    flag_positions.emplace_back(p.x_,p.y_-2,p.z_);
                }
                else if(r<0.4)
                {
                    auto p=wp.node->node->GetChild(wp.spawn_points[0],true)->GetWorldPosition();
                    torch_positions.emplace_back(p.x_,p.y_-1.5,p.z_);
                }
                else if(r<0.6)
                {
                    auto p=wp.node->node->GetChild(wp.spawn_points[0],true)->GetWorldPosition();
                    enemy_positions.emplace_back(p.x_,p.y_-1.5,p.z_);
                }
                else
                for(int i=0;i<Random(7);i++)
                {
                    PhysicsRaycastResult result;
                    auto pos=wp.node->node->GetChild(wp.spawn_points[0],true)->GetWorldPosition();
                    pos+=Vector3(Random(-1.0f,1.0f),1,Random(-1.0f,1.0f));
                    Ray ray(pos,Vector3(0,-1,0));
                    globals::instance()->physical_world->SphereCast(result,ray,0.2,100);
                    if(result.distance_<=1000)
                        pos=result.position_+Vector3(0,0.1,0);

                    auto node_stone=globals::instance()->scene->CreateChild("Stone");
                    gs_playing::instance->nodes.emplace_back(node_stone);
                    StaticModel* boxObject=node_stone->CreateComponent<StaticModel>();
                    boxObject->SetModel(globals::instance()->cache->GetResource<Model>("Models/rock.mdl"));
                    boxObject->SetMaterial(globals::instance()->cache->GetResource<Material>("Materials/rock.xml"));
                    boxObject->SetCastShadows(true);
                    float s=0.2+Random(0.4f);
                    node_stone->SetScale(s);

                    auto body_stone=node_stone->CreateComponent<RigidBody>();
                    body_stone->SetPosition(pos);
                    body_stone->SetCollisionLayer(3);
                    body_stone->SetMass(50.0*s*s);
                    body_stone->SetLinearDamping(0.2f);
                    body_stone->SetAngularDamping(0.2f);
                    body_stone->SetFriction(0.6);
                    CollisionShape* shape=node_stone->CreateComponent<CollisionShape>();
                    shape->SetConvexHull(globals::instance()->cache->GetResource<Model>("Models/rock.mdl"));

                    gs_playing::instance->SubscribeToEvent(node_stone,E_NODECOLLISIONSTART,new Urho3D::EventHandlerImpl<gs_playing>(gs_playing::instance,&gs_playing::HandleStoneCollision));
                }
            }

            world_parts.push_back(wp);
        }

        fix_occupied_ports();
        place_end_pieces();

        globals::instance()->physical_world->Update(5); // let all physical stuff settle a bit.
    }
    player_pos=Vector3(0,50,0);
}

class vec3
{
public:
    int x_,y_,z_;   // ints to round the coordinates and prevent floating point fluctuation

    vec3(const Vector3& o) : x_(round(o.x_)),y_(round(o.y_)),z_(round(o.z_)) {}
    bool operator<(const vec3& rhs) const
    {
        if(x_<rhs.x_)
            return true;
        if(x_>rhs.x_)
            return false;

        if(y_<rhs.y_)
            return true;
        if(y_>rhs.y_)
            return false;

        if(z_<rhs.z_)
            return true;
        if(z_>rhs.z_)
            return false;

        return false;
    }
};

void level::fix_occupied_ports()
{
    std::multimap<vec3,std::pair<world_part*,String>> open_ports;
    int wp_count=world_parts.size();
    for(int i=0;i<wp_count;i++)
    {
        world_part* wp=world_parts.data()+i;
        for(String dp:wp->docking_points)
        {
            if(wp->docking_points_occupied.find(dp)==wp->docking_points_occupied.end())
                open_ports.insert(std::make_pair(vec3(wp->node->node->GetChild(dp,true)->GetWorldPosition()),std::make_pair(wp,dp)));
        }
    }

    for(std::multimap<vec3,std::pair<world_part*,String>>::iterator e=open_ports.begin();e!=open_ports.end();e++)
    {
        if(open_ports.count(e->first)>=2)
        {
            auto iter=open_ports.equal_range(e->first);
            decltype(iter.first) it=iter.first;
            world_part* wp_first=it->second.first;
            String dock_first=it->second.second;
            it++;
            for(;it!=iter.second;it++)
            {
                world_part* wp=it->second.first;
                String dock=it->second.second;
                wp_first->docking_points_occupied.insert(dock_first);
                wp->docking_points_occupied.insert(dock);
            }
        }
        for(int i=0;i<open_ports.count(e->first)-1;i++)
            e++;
    }
}

void level::place_end_pieces()
{
    int wp_count=world_parts.size();
    for(int i=0;i<wp_count;i++)
    {
        world_part wp=world_parts[i];   // WEIRD: if I don't make a copy here (aka pointer or reference) wp is sometimes broken. This works via value for some reason.
        for(String dp:wp.docking_points)
        {
            world_part wp_new("mineshaft_end");
            if(wp_new.move_to_docking_point("dock_mineshaft_0",wp,dp,true))
                world_parts.push_back(wp_new);
        }
    }
}

void level::load_lua_level(std::string level_filename)
{
    std::cout<<"loading level \""<<level_filename<<"\" via LUA script"<<std::endl;

    // TODO: Execute the LUA script after having given some functions which can be called by LUA.
    // Currently it could just fill this level object like the XML loading function above does.
    // Could use some kind of instance if member functions can't be bound directly.
}

///////////////

gs_playing* gs_playing::instance;
std::string gs_playing::last_level_filename;

gs_playing::gs_playing(std::string level_filename) : game_state()
{
    if(instance)
        instance->~gs_playing();
    instance=this;
    last_level_filename=level_filename;
    // create a transparent window with some text to display things like level time, remaining flags and FPS
    {
        Window* window=new Window(context_);
        gui_elements.push_back(window);
        GetSubsystem<UI>()->GetRoot()->AddChild(window);
        window->SetStyle("Window");
        window->SetSize(700,100);
        window->SetColor(Color(.0,.15,.3,.5));
        window->SetAlignment(HA_CENTER,VA_TOP);

        window_text=new Text(context_);
        window_text->SetFont(globals::instance()->cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"),16);
        window_text->SetColor(Color(.8,.85,.9));
        window_text->SetAlignment(HA_CENTER,VA_CENTER);
        window->AddChild(window_text);
    }

    GetSubsystem<Input>()->SetMouseVisible(false);
    GetSubsystem<Input>()->SetMouseGrabbed(true);

    SubscribeToEvent(E_UPDATE,URHO3D_HANDLER(gs_playing,update));
    SubscribeToEvent(E_KEYDOWN,URHO3D_HANDLER(gs_playing,HandleKeyDown));
    SubscribeToEvent(E_MOUSEBUTTONDOWN,URHO3D_HANDLER(gs_playing,HandleMouseDown));
    SubscribeToEvent(E_MOUSEBUTTONUP,URHO3D_HANDLER(gs_playing,HandleMouseUp));

    current_level=level(level_filename);
    for(auto& sm:current_level.static_models)
    {
        Node* boxNode_=globals::instance()->scene->CreateChild();
        nodes.emplace_back(boxNode_);
        boxNode_->SetPosition(sm.pos);
        boxNode_->SetScale(Vector3(sm.scale,sm.scale,sm.scale));
        StaticModel* boxObject=boxNode_->CreateComponent<StaticModel>();
        set_model(boxObject,globals::instance()->cache,std::string(sm.name.CString(),sm.name.Length()));
        boxObject->SetCastShadows(true);
        boxObject->SetOccludee(true);
        boxObject->SetOccluder(true);

        float min_y=boxObject->GetWorldBoundingBox().min_.y_;
        if(current_level.level_min_height)
            level_min_height=current_level.level_min_height;
        else
            if(level_min_height>min_y)
                level_min_height=min_y;

        RigidBody* body=boxNode_->CreateComponent<RigidBody>();
        body->SetCollisionLayer(2); // Use layer bitmask 2 for static geometry
        CollisionShape* shape=boxNode_->CreateComponent<CollisionShape>();
        shape->SetTriangleMesh(globals::instance()->cache->GetResource<Model>(sm.name+".mdl"));

        globals::instance()->physical_world->SetGravity(Vector3(0,current_level.gravity,0));
    }
    if(current_level.sound_name.Length())
    {
        Node* n=globals::instance()->scene->CreateChild();
        nodes.emplace_back(n);
        auto sound=globals::instance()->cache->GetResource<Sound>(current_level.sound_name);
        sound->SetLooped(true);
        auto sound_source=n->CreateComponent<SoundSource>();
        sound_source->SetSoundType(SOUND_MUSIC);
        sound_source->Play(sound);
    }

    player_.reset(new player(current_level.player_pos,this));
    {   // "load" flags
        for(auto p:current_level.flag_positions)
        {
            Node* n=globals::instance()->scene->CreateChild("Flag");
            nodes.emplace_back(n);

            n->SetPosition(p);
            StaticModel* boxObject=n->CreateComponent<StaticModel>();
            set_model(boxObject,globals::instance()->cache,"Data/Models/flag");
            boxObject->SetCastShadows(true);
            flag_nodes.push_back(n);
        }

        for(auto p:current_level.torch_positions)
            spawn_torch(p);

        for(auto p:current_level.enemy_positions)
            enemies.emplace_back(new enemy(p));
    }
    // skybox
    {
        Node* skyNode=globals::instance()->scene->CreateChild("Sky");
        nodes.emplace_back(skyNode);
        skyNode->SetScale(50000.0f);
        Skybox* skybox=skyNode->CreateComponent<Skybox>();
        skybox->SetModel(globals::instance()->cache->GetResource<Model>("Models/Box.mdl"));
        skybox->SetMaterial(globals::instance()->cache->GetResource<Material>(current_level.skybox_material));
    }

    // sun
    {
        Node* lightNode=globals::instance()->scene->CreateChild("Light");
        nodes.emplace_back(lightNode);
        Light* light=lightNode->CreateComponent<Light>();
        light->SetLightType(LIGHT_DIRECTIONAL);
        light->SetCastShadows(true);
        light->SetShadowBias(BiasParameters(0.00000025f,1.0f));
        light->SetShadowCascade(CascadeParameters(20.0f,60.0f,180.0f,560.0f,100.0f,100.0f));
        light->SetShadowResolution(1.0);
        light->SetBrightness(1.2);
        light->SetColor(Color(0.02,0.05,0.1,1));
        lightNode->SetDirection(Vector3::FORWARD);
        lightNode->Yaw(-150);   // horizontal
        lightNode->Pitch(60);   // vertical
        lightNode->Translate(Vector3(0,0,-20000));

        BillboardSet* billboardObject=lightNode->CreateComponent<BillboardSet>();
        billboardObject->SetNumBillboards(1);
        billboardObject->SetMaterial(globals::instance()->cache->GetResource<Material>("Materials/moon.xml"));
        billboardObject->SetSorted(true);
        Billboard* bb=billboardObject->GetBillboard(0);
        bb->size_=Vector2(10000,10000);
        bb->rotation_=Random()*360.0f;
        bb->enabled_=true;
        billboardObject->Commit();
    }

    // spawn one rock and remove it to cache the collider mesh (to avoid a ~1 second lag when spawning the first rock during the game)
    {
        auto node_stone=globals::instance()->scene->CreateChild("Stone");
        StaticModel* boxObject=node_stone->CreateComponent<StaticModel>();
        boxObject->SetModel(globals::instance()->cache->GetResource<Model>("Models/rock.mdl"));
        boxObject->SetMaterial(globals::instance()->cache->GetResource<Material>("Materials/rock.xml"));
        boxObject->SetCastShadows(true);
        float s=1.0+Random(3.0f);
        node_stone->SetScale(s);

        PhysicsRaycastResult result;
        Vector3 pos(-120-Random(100.0f),100,-70-Random(100.0f));
        Ray ray(pos,Vector3(0,-1,0));
        globals::instance()->physical_world->SphereCast(result,ray,2,100);
        if(result.distance_<=1000)
            pos=result.position_+Vector3(0,5,0);

        auto body_stone=node_stone->CreateComponent<RigidBody>();
        body_stone->SetPosition(pos);
        body_stone->SetCollisionLayer(2);
        body_stone->SetMass(50.0*s*s);
        body_stone->SetLinearDamping(0.2f);
        body_stone->SetAngularDamping(0.2f);
        body_stone->SetFriction(0.6);
        CollisionShape* shape=node_stone->CreateComponent<CollisionShape>();
        shape->SetConvexHull(globals::instance()->cache->GetResource<Model>("Models/rock.mdl"));
        node_stone->Remove();
    }

    timer_playing=0;

    // spawn some enemies
    for(int i=0;i<0;i++)
    {
        PhysicsRaycastResult result;
        Vector3 pos(0,10,0);
        pos.x_=Random(-100,100);
        pos.z_=Random(-100,100);
        Ray ray(pos,Vector3(0,-1,0));
        globals::instance()->physical_world->SphereCast(result,ray,2,200,2);
        if(result.distance_<=1000)
            pos=result.position_+Vector3(0,5,0);

        enemies.emplace_back(new enemy(pos));
    }

    sound_stone_collision=globals::instance()->cache->GetResource<Sound>("Sounds/stone_fall.wav");
}

void gs_playing::update(StringHash eventType,VariantMap& eventData)
{
    if(globals::instance()->game_states.size()>1)
        return;

    delayed_actions.update();

    Input* input=GetSubsystem<Input>();
    float timeStep=eventData[Update::P_TIMESTEP].GetFloat();
    timer_playing+=timeStep;

    std::string str;
    {
        static double last_second=0;
        static double last_second_frames=1;
        static timer this_second;
        static double this_second_frames=0;
        this_second_frames++;
        if(this_second.until_now()>=1)
        {
            last_second=this_second.until_now();
            last_second_frames=this_second_frames;
            this_second.reset();
            this_second_frames=0;
        }

        if(last_second!=0)
            str.append(std::to_string(last_second_frames/last_second).substr(0,6));
        str.append(" FPS   Position: ");
        str.append(std::to_string(player_->node->GetPosition().x_).substr(0,6));
        str.append(", ");
        str.append(std::to_string(player_->node->GetPosition().y_).substr(0,6));
        str.append(", ");
        str.append(std::to_string(player_->node->GetPosition().z_).substr(0,6));
        str.append("\nLevel Time: ");

        if(goal_time>0)
            str.append(std::to_string(goal_time));
        else
            str.append(std::to_string(timer_playing));

        str.append("s Remaining Flags: ");
        str.append(std::to_string(flag_nodes.size()));
        str.append("/");
        str.append(std::to_string(current_level.flag_positions.size()));
        str.append("\nUse WASD and shift to move and F to toggle flashlight.");
        if(goal_time>0)
            str.append("\nFinished!");

        String s(str.c_str(),str.size());
        window_text->SetText(s);
    }

    player_->update(input,timeStep);

    Vector3 player_pos=player_->node->GetPosition();
    for(int i=0;i<flag_nodes.size();i++)
    {
        auto n=flag_nodes[i];
        n->Yaw(64*timeStep);
        if((player_pos-n->GetPosition()).Length()<2)
        {
            //player_->sound_source_flag->Play(player_->sound_flag);
            flag_nodes.erase(flag_nodes.begin()+i);
            n->Remove();
            for(int j=0;j<nodes.size();j++)
                if(nodes[j]==n)
                {
                    nodes.erase(nodes.begin()+j);
                    break;
                }

            if(flag_nodes.size()==0)
            {
                goal_time=timer_playing;

                auto e=new gs_level_end;
                std::string str="You collected all flags!\nNeeded time: "+std::to_string(goal_time)+"s";

                map_times highscores;
                // update if time not set or time better as the current highscore
                if(highscores.get(last_level_filename)<1||goal_time<highscores.get(last_level_filename))
                {
                    highscores.insert(last_level_filename,goal_time);
                    str+="\nNew record!";
                }
                highscores.save();

                e->text_finished->SetText(str.c_str());
                globals::instance()->game_states.emplace_back(e);
            }
            break;
        }
    }

    if(player_->node->GetWorldPosition().y_<level_min_height-10)    // die if below level geometry
        globals::instance()->game_states.emplace_back(new gs_death);

    if(grabbed_body)
    {
        Vector3 target=globals::instance()->camera->GetNode()->GetWorldPosition()+globals::instance()->camera->GetNode()->GetWorldDirection()*grab_distance;
        Vector3 source=grabbed_body->GetPosition();
        grabbed_body->ApplyImpulse((target-source)*500*timeStep-grabbed_body->GetLinearVelocity()*0.2*grabbed_body->GetMass());
        //if((target-source).Length()>1)
        //    grabbed_body=0;
    }
    else
    {
        PhysicsRaycastResult result;
        Ray ray=globals::instance()->camera->GetScreenRay(0.5,0.5);
        globals::instance()->physical_world->SphereCast(result,ray,0.1,2);
        if(result.body_&&result.distance_<2&&result.body_!=player_->body)
        {
            if(!highlighted_body)
            {
                highlighted_body=result.body_;
                Node* n=result.body_->GetNode();
                if(!n)
                    return;
                StaticModel* m=n->GetComponent<StaticModel>();
                if(!m)
                    return;
                highlight_old_materials.clear();
                for(int i=0;i<m->GetNumGeometries();i++)
                    highlight_old_materials.push_back(m->GetMaterial(i));
                m->SetMaterial(globals::instance()->cache->GetResource<Material>("Materials/highlight.xml"));
            }
            return;
        }
    }
    if(highlighted_body)
    {
        Node* n=highlighted_body->GetNode();
        if(n)
        {
            StaticModel* m=n->GetComponent<StaticModel>();
            if(m)
            {
                for(int i=0;i<highlight_old_materials.size();i++)
                    m->SetMaterial(i,highlight_old_materials[i]);
                highlight_old_materials.clear();
            }
        }
        highlighted_body=0;
    }
}

void gs_playing::HandleKeyDown(StringHash eventType,VariantMap& eventData)
{
    if(globals::instance()->game_states.size()>1)
        return;
    using namespace KeyDown;
    int key=eventData[P_KEY].GetInt();
    if(key==KEY_ESCAPE)
        globals::instance()->game_states.emplace_back(new gs_pause);

    if(key==KEY_L)
        spawn_torch(player_->node->GetPosition()+Vector3(2,1.9,0));
    else if(key==KEY_V)
        player_->camera_first_person=!player_->camera_first_person;
    else if(key==KEY_F)
    {
        player_->sound_source_flashlight_button->Play(player_->sound_flashlight_button);
        delayed_actions.insert(0.02,[this]{player_->light->SetBrightness(player_->light->GetBrightness()>0.5?0:1.5);});
    }
    else if(key==KEY_E)
        enemies.emplace_back(new enemy(player_->node->GetPosition()+Vector3(5,2,0)));
    else if(key==KEY_P)
        player_->body->SetPosition(player_->body->GetPosition()+Vector3(0,20,0));
    else if(key==KEY_T)
        globals::instance()->camera->SetFillMode(globals::instance()->camera->GetFillMode()==FILL_WIREFRAME?FILL_SOLID:FILL_WIREFRAME);
}

void gs_playing::HandleMouseDown(StringHash eventType,VariantMap& eventData)
{
    PhysicsRaycastResult result;
    Ray ray=globals::instance()->camera->GetScreenRay(0.5,0.5);
    globals::instance()->physical_world->SphereCast(result,ray,0.1,10);
    if(result.body_&&result.distance_<2&&result.body_!=player_->body)
    {
        grabbed_body=result.body_;
        grab_distance=(result.body_->GetPosition()-globals::instance()->camera->GetNode()->GetWorldPosition()).Length();
    }
}

void gs_playing::HandleMouseUp(StringHash eventType,VariantMap& eventData)
{
    grabbed_body=0;
}

void gs_playing::HandleStoneCollision(StringHash eventType,VariantMap& eventData)
{
    auto body=(RigidBody*)eventData["Body"].GetVoidPtr();
    Node* node=body->GetNode();
    auto sound_source=node->CreateComponent<SoundSource3D>();
    sound_source->SetNearDistance(1);
    sound_source->SetFarDistance(55);
    sound_source->SetSoundType(SOUND_EFFECT);
    sound_source->SetFrequency(44100/(node->GetScale().x_*3.0));
    sound_source->SetGain(std::min(1.0,1.5*body->GetLinearVelocity().Length()));
    sound_source->Play(sound_stone_collision);
}

void gs_playing::spawn_torch(Vector3 pos)
{
    Node* node=globals::instance()->scene->CreateChild();
    nodes.emplace_back(node);

    PhysicsRaycastResult result;
    Ray ray(pos,Vector3(0,-1,0));
    globals::instance()->physical_world->SphereCast(result,ray,0.4,10);
    if(result.distance_<=10)
        pos=result.position_+Vector3(0,0.2,0);
    node->SetPosition(pos);

    StaticModel* boxObject=node->CreateComponent<StaticModel>();
    set_model(boxObject,globals::instance()->cache,"Data/Models/torch");
    boxObject->SetCastShadows(true);
    boxObject->SetOccludee(true);
    boxObject->SetShadowDistance(200);
    boxObject->SetDrawDistance(200);

    auto lightNode=node->CreateChild();
    lightNode->Translate(Vector3(0,2,0));
    Light* light=lightNode->CreateComponent<Light>();
    light->SetLightType(LIGHT_POINT);
    light->SetRange(50);
    light->SetBrightness(1.5);
    light->SetColor(Color(2.0,1.2,.8,1.0));
    light->SetCastShadows(true);
    light->SetShadowDistance(300);
    light->SetDrawDistance(300);

    auto body_stone=node->CreateComponent<RigidBody>();
    body_stone->SetCollisionLayer(2);
    body_stone->SetMass(50.0);
    body_stone->SetLinearDamping(0.2f);
    body_stone->SetAngularDamping(0.2f);
    body_stone->SetFriction(0.6);
    CollisionShape* shape=node->CreateComponent<CollisionShape>();
    shape->SetBox(Vector3(0.7,1.47,0.7),Vector3(0,1.47/2,0));

    auto n_particle=node->CreateChild();
    n_particle->Translate(Vector3(0,1.6,0));
    ParticleEmitter* emitter=n_particle->CreateComponent<ParticleEmitter>();
    emitter->SetEffect(globals::instance()->cache->GetResource<ParticleEffect>("Particle/torch_fire.xml"));
    emitter=n_particle->CreateComponent<ParticleEmitter>();
    emitter->SetEffect(globals::instance()->cache->GetResource<ParticleEffect>("Particle/torch_smoke.xml"));

    auto sound_torch=globals::instance()->cache->GetResource<Sound>("Sounds/torch.ogg");
    sound_torch->SetLooped(true);
    auto sound_torch_source=n_particle->CreateComponent<SoundSource3D>();
    sound_torch_source->SetNearDistance(1);
    sound_torch_source->SetFarDistance(50);
    sound_torch_source->SetSoundType(SOUND_EFFECT);
    sound_torch_source->Play(sound_torch);
    sound_torch_source->SetFrequency(sound_torch->GetFrequency()*Random(0.7f,1.3f));
}
