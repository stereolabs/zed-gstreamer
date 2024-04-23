#ifdef __aarch64__
#include <ArgusComponent.hpp>

using namespace oc;

ArgusProvider* ArgusProvider::instance = nullptr;
Argus::UniqueObj<Argus::CameraProvider> ArgusProvider::cameraProvider = Argus::UniqueObj<Argus::CameraProvider>(0);
ARGUS_CAMERA_STATE ArgusProvider::camera_states[MAX_GMSL_CAMERAS] = {ARGUS_CAMERA_STATE::OFF};//=ARGUS_CAMERA_STATE::OFF;



ArgusProvider *ArgusProvider::getInstance()
{
    if(instance == nullptr)
    {
        instance = new ArgusProvider();
        cameraProvider=Argus::UniqueObj<Argus::CameraProvider> (Argus::CameraProvider::create());
        for (int i=0;i<MAX_GMSL_CAMERAS;i++)
            camera_states[i]=ARGUS_CAMERA_STATE::OFF;

    }
    return instance;
}

void ArgusProvider::DeleteInstance()
{
    if(instance != nullptr)
        delete instance;
    instance = nullptr;
    cameraProvider.reset();
    for (int i=0;i<MAX_GMSL_CAMERAS;i++)
        camera_states[i]=ARGUS_CAMERA_STATE::OFF;
}

void ArgusProvider::changeState(int id, ARGUS_CAMERA_STATE state)
{
    if (id>=0 && id<MAX_GMSL_CAMERAS)
    camera_states[id] = state;
}

ARGUS_CAMERA_STATE ArgusProvider::getState(int id)
{
    if (id>=0 && id<MAX_GMSL_CAMERAS)
    return camera_states[id];
    else {
       return ARGUS_CAMERA_STATE::OFF;
    }
}

bool ArgusProvider::hasCameraOpening()
{
    bool res = false;
    for (int i=0;i<MAX_GMSL_CAMERAS;i++)
    {
        if (camera_states[i] == ARGUS_CAMERA_STATE::OPENING)
            res = true;

    }
    return res;
}

bool ArgusProvider::hasCameraFrozen()
{
    bool res = false;
    for (int i=0;i<MAX_GMSL_CAMERAS;i++)
    {
        if (camera_states[i] == ARGUS_CAMERA_STATE::FROZEN)
            res = true;
    }
    return res;
}




#endif
