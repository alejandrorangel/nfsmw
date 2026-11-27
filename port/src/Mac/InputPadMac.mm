// The gamepad's name for the PC controller screen (the PC shows the
// DirectInput joystick's product name, sub_628C60).

#import <GameController/GameController.h>

#include <mutex>
#include <string>

const char *PortInputGetPadName() {
    static std::mutex lock;
    static std::string name;
    std::lock_guard<std::mutex> guard(lock);
    GCController *controller = GCController.current;
    if (controller == nil && GCController.controllers.count > 0) {
        controller = GCController.controllers.firstObject;
    }
    NSString *vendor = controller.vendorName;
    name = vendor != nil ? vendor.UTF8String : (controller != nil ? "Gamepad" : "");
    return name.c_str();
}
