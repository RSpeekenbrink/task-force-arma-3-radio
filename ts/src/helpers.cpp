#include "helpers.hpp"

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#include <algorithm>
#include "task_force_radio.hpp"
#include <bitset>

#define CAN_USE_SSE_ON(x) (IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE) && (reinterpret_cast<uintptr_t>(x) % 16 == 0))

void helpers::applyGain(short * samples, size_t sampleCount, int channels, float directTalkingVolume) {
    if (directTalkingVolume == 0.0f) {
        memset(samples, 0, sampleCount * channels * sizeof(short));
        return;
    }
    if (directTalkingVolume == 1.0f) //no change in gain
        return;



    size_t leftOver = sampleCount * channels;
    if (CAN_USE_SSE_ON(samples)) {
        leftOver = (sampleCount * channels) % 8;
        __m128 xmm3;
        float multiplier[4] = { directTalkingVolume ,directTalkingVolume , directTalkingVolume, directTalkingVolume };//This is limiting to 4 channels max. But If we implement surround sound we don't really need a center
        xmm3 = _mm_loadu_ps(multiplier);
        helpers::shortFloatMultEx(samples, (sampleCount * channels) - leftOver, xmm3);
    }
    for (size_t i = sampleCount * channels - leftOver; i < sampleCount * channels; i++) samples[i] = static_cast<short>(samples[i] * directTalkingVolume);
}

constexpr float DegToRad(float deg) {
    return deg * (static_cast<float>(M_PI) / 180);
}
//static_assert(static_cast<AngleRadians>(190.0_deg) > 3.f, "");
void helpers::applyILD(short * samples, size_t sampleCount, int channels, Direction3D direction, AngleRadians viewAngle) {
    if (channels == 2) {
        AngleRadians dir = (direction.toPolarAngle() + viewAngle);
        float gainFrontLeft = AngleDegrees(-21.5f).toRadians() * dir.cosine() + 0.625f;
        float gainFrontRight = AngleDegrees(21.5f).toRadians() * dir.cosine() + 0.625f;
        //Use https://msdn.microsoft.com/en-us/library/windows/desktop/ee415798(v=vs.85).aspx for more than 2 channels

        size_t leftOver = sampleCount * channels;
        if (CAN_USE_SSE_ON(samples)) {
            leftOver = (sampleCount * channels) % 8;
            float multiplier[4] = { gainFrontLeft ,gainFrontRight , gainFrontLeft, gainFrontRight };//This is limiting to 4 channels max. But If we implement surround sound we don't really need a center
            __m128 xmm3 = _mm_loadu_ps(multiplier);
            helpers::shortFloatMultEx(samples, (sampleCount * channels) - leftOver, xmm3);
        }
        for (size_t i = sampleCount * channels - leftOver; i < sampleCount * channels; i += channels) {
            samples[i] = static_cast<short>(samples[i] * gainFrontLeft);
            samples[i + 1] = static_cast<short>(samples[i + 1] * gainFrontRight);
        }
    }
}
#define _SPEAKER_POSITIONS_
#include <X3daudio.h>
#pragma comment(lib, "x3daudio.lib")
X3DAUDIO_HANDLE x3d_handle;
bool x3d_initialized = false;

void helpers::applyILD(short * samples, size_t sampleCount, int channels, Position3D myPosition, Direction3D myViewDirection, Position3D emitterPosition, Direction3D emitterViewDirection, bool shouldPlayerHear, int emitterVoiceVolume) {
    if (!x3d_initialized) {
        X3DAudioInitialize(
            SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT,
            X3DAUDIO_SPEED_OF_SOUND,
            x3d_handle
        );
        x3d_initialized = true;
    }
    //#X3DAudio cache if position didn't change.
    //#X3DAudio make player local effect so X3DAudio objects are local to every player.
    //#X3DAudio fix up vector problem
    X3DAUDIO_LISTENER listener{};


   
    std::tie(listener.OrientFront.x, listener.OrientFront.y, listener.OrientFront.z) = myViewDirection.get();
    //listener.OrientFront.z = std::clamp(listener.OrientFront.z, -0.65f, 1.f);
    //listener.OrientFront.x = listener.OrientFront.x;//East and West are mixed up
    //listener.OrientFront.y = listener.OrientFront.y;//North and South are mixed up   
    //listener.OrientFront.z = 0;


    /*
onEachFrame {

drawLine3D [ASLToAGL eyePos player2, ASLToAGL (eyePos player2) vectorAdd ((getCameraViewDirection player2) vectorMultiply 20), [1,0,1,1]];

rightVec = (getCameraViewDirection player2) vectorCrossProduct [0,0,1];
drawLine3D [ASLToAGL eyePos player2, ASLToAGL (eyePos player2) vectorAdd (rightVec vectorMultiply 20), [1,0,1,1]];

upVec = rightVec vectorCrossProduct (getCameraViewDirection player2);
drawLine3D [ASLToAGL eyePos player2, ASLToAGL (eyePos player2) vectorAdd (upVec vectorMultiply 20), [1,0,1,1]];
}
     */

    auto myRightVector = myViewDirection.crossProduct({ 0,0,1 });
    auto myUpVector = myRightVector.crossProduct(myViewDirection);

    //listener.OrientFront = { 0,-1,0 };



    std::tie(listener.OrientTop.x, listener.OrientTop.y, listener.OrientTop.z) = myUpVector.get();// Direction3D(0/*-std::get<0>(myViewDirection.get())*/, -std::get<2>(myViewDirection.get()), std::get<1>(myViewDirection.get())).get();
    
    //std::tie(listener.OrientTop.x, listener.OrientTop.y, listener.OrientTop.z) = Direction3D(-std::get<0>(myViewDirection.get()), -std::get<1>(myViewDirection.get()), 1.f).get();

    
    
    //listener.OrientTop = { 0,0,1 };
    std::tie(listener.Position.x, listener.Position.y, listener.Position.z) = emitterPosition.get();
    listener.pCone = nullptr;

    X3DAUDIO_EMITTER emitter{};
    X3DAUDIO_CONE emitterCone{};

    emitterCone.InnerAngle = DegToRad(90);
    emitterCone.OuterAngle = DegToRad(135); //https://www.researchgate.net/figure/Directivity-diagrams-Top-row-Azimuth-directivity-diagrams-for-EE-mouth-clicks-at-40cm_fig3_319413904
    emitterCone.InnerVolume = 1.8f;
    emitterCone.OuterVolume = 1.f;

    emitter.pCone = &emitterCone;
    std::tie(emitter.Position.x, emitter.Position.y, emitter.Position.z) = myPosition.get();
    std::tie(emitter.OrientFront.x, emitter.OrientFront.y, emitter.OrientFront.z) = emitterViewDirection.normalized().get();
    emitter.OrientFront.y *= -1.f;
    emitter.OrientFront.x *= -1.f;

    auto emitterRightVector = emitterViewDirection.normalized().crossProduct({ 0,0,1 });
    auto emitterUpVector = emitterRightVector.crossProduct(emitterViewDirection.normalized());
    //emitter.OrientFront = { 0,1,0 };
    std::tie(emitter.OrientTop.x, emitter.OrientTop.y, emitter.OrientTop.z) = emitterUpVector.get();
    emitter.ChannelCount = 1;
    emitter.CurveDistanceScaler = 1.f;
    emitter.ChannelRadius = 1.f;

    const float volumePerMeter = emitterVoiceVolume / 19.f;
    std::array<X3DAUDIO_DISTANCE_CURVE_POINT, 20> distanceCurves{
        X3DAUDIO_DISTANCE_CURVE_POINT{0.f, 1.f},
        X3DAUDIO_DISTANCE_CURVE_POINT{ volumePerMeter, volumeAttenuation(volumePerMeter, shouldPlayerHear, emitterVoiceVolume) },
        X3DAUDIO_DISTANCE_CURVE_POINT{ volumePerMeter*2, volumeAttenuation(volumePerMeter*2, shouldPlayerHear, emitterVoiceVolume) },
        X3DAUDIO_DISTANCE_CURVE_POINT{ volumePerMeter*3, volumeAttenuation(volumePerMeter*3, shouldPlayerHear, emitterVoiceVolume) },
        X3DAUDIO_DISTANCE_CURVE_POINT{ volumePerMeter*4, volumeAttenuation(volumePerMeter*4, shouldPlayerHear, emitterVoiceVolume) },
        X3DAUDIO_DISTANCE_CURVE_POINT{ volumePerMeter*5, volumeAttenuation(volumePerMeter*5, shouldPlayerHear, emitterVoiceVolume) },
        X3DAUDIO_DISTANCE_CURVE_POINT{ volumePerMeter*6, volumeAttenuation(volumePerMeter*6, shouldPlayerHear, emitterVoiceVolume) },
        X3DAUDIO_DISTANCE_CURVE_POINT{ volumePerMeter*7, volumeAttenuation(volumePerMeter*7, shouldPlayerHear, emitterVoiceVolume) },
        X3DAUDIO_DISTANCE_CURVE_POINT{ volumePerMeter*8, volumeAttenuation(volumePerMeter*8, shouldPlayerHear, emitterVoiceVolume) },
        X3DAUDIO_DISTANCE_CURVE_POINT{ volumePerMeter*9, volumeAttenuation(volumePerMeter*9, shouldPlayerHear, emitterVoiceVolume) },
        X3DAUDIO_DISTANCE_CURVE_POINT{ volumePerMeter*10, volumeAttenuation(volumePerMeter*10, shouldPlayerHear, emitterVoiceVolume) },
        X3DAUDIO_DISTANCE_CURVE_POINT{ volumePerMeter*11, volumeAttenuation(volumePerMeter*11, shouldPlayerHear, emitterVoiceVolume) },
        X3DAUDIO_DISTANCE_CURVE_POINT{ volumePerMeter*12, volumeAttenuation(volumePerMeter*12, shouldPlayerHear, emitterVoiceVolume) },
        X3DAUDIO_DISTANCE_CURVE_POINT{ volumePerMeter*13, volumeAttenuation(volumePerMeter*13, shouldPlayerHear, emitterVoiceVolume) },
        X3DAUDIO_DISTANCE_CURVE_POINT{ volumePerMeter*14, volumeAttenuation(volumePerMeter*14, shouldPlayerHear, emitterVoiceVolume) },
        X3DAUDIO_DISTANCE_CURVE_POINT{ volumePerMeter*15, volumeAttenuation(volumePerMeter*15, shouldPlayerHear, emitterVoiceVolume) },
        X3DAUDIO_DISTANCE_CURVE_POINT{ volumePerMeter*16, volumeAttenuation(volumePerMeter*16, shouldPlayerHear, emitterVoiceVolume) },
        X3DAUDIO_DISTANCE_CURVE_POINT{ volumePerMeter*17, volumeAttenuation(volumePerMeter*17, shouldPlayerHear, emitterVoiceVolume) },
        X3DAUDIO_DISTANCE_CURVE_POINT{ volumePerMeter*18, volumeAttenuation(volumePerMeter*18, shouldPlayerHear, emitterVoiceVolume) },
        X3DAUDIO_DISTANCE_CURVE_POINT{ volumePerMeter*19, volumeAttenuation(volumePerMeter*19, shouldPlayerHear, emitterVoiceVolume) }
    };
    X3DAUDIO_DISTANCE_CURVE dCurve;
    dCurve.pPoints = distanceCurves.data();
    dCurve.PointCount = distanceCurves.size();
    emitter.pVolumeCurve = &dCurve;


    X3DAUDIO_DSP_SETTINGS output{ 0 };

    output.SrcChannelCount = 1;
    output.DstChannelCount = channels;
    float* volumeMatrix = new float[channels];//#X3DAudio minimum 2 channels
    output.pMatrixCoefficients = volumeMatrix;



    X3DAudioCalculate(
        x3d_handle,
        &listener,
        &emitter,
        X3DAUDIO_CALCULATE_MATRIX,
        &output
    );



    float gainFrontLeft = volumeMatrix[0];
    float gainFrontRight = volumeMatrix[1];
    delete[] volumeMatrix;
    const float totalVolume = (gainFrontRight + gainFrontLeft);
    float mult = 1.35f;
    if (totalVolume > 1.f) {
        mult *= 1.f / totalVolume;
    }
    //OutputDebugStringA((std::to_string(gainFrontLeft) + "_" + std::to_string(gainFrontRight) + "_" + std::to_string(mult)+" total: "+std::to_string(totalVolume*mult)+ "\n").c_str());
    gainFrontLeft *= mult; //make sure left+right = 1.25 else it would decrease overall volume too much which we don't want
    gainFrontRight *= mult;

    size_t leftOver = sampleCount * channels;
    if (CAN_USE_SSE_ON(samples)) { //Can use SSE and memory is correctly aligned
        leftOver = (sampleCount * channels) % 8;
        __m128 xmm3;
        float multiplier[4] = { gainFrontLeft ,gainFrontRight , gainFrontLeft, gainFrontRight };//This is limiting to 4 channels max. But If we implement surround sound we don't really need a center
        xmm3 = _mm_loadu_ps(multiplier);
        helpers::shortFloatMultEx(samples, (sampleCount * channels) - leftOver, xmm3);
    }
    for (size_t i = sampleCount * channels - leftOver; i < sampleCount * channels; i += channels) {
        samples[i] = static_cast<short>(samples[i] * gainFrontLeft);
        samples[i + 1] = static_cast<short>(samples[i + 1] * gainFrontRight);
    }
}

void helpers::shortFloatMultEx(short * data, size_t elementCount, __m128 multPack) {//#TODO use in gain and ILD for ILD multPack is {left,right,left,right}

    /*
    Thanks GCC!
    https://godbolt.org/#g:!((g:!((g:!((h:codeEditor,i:(j:1,source:'%23include+%3Calgorithm%3E%0Ashort+square(int+num)+%7B%0A++++short+samples%5B4096%5D%3B%0A%0A++++for+(size_t+i+%3D+0%3B+i+%3C+4096%3B+i%2B%2B)+samples%5Bi%5D+%3D+static_cast%3Cshort%3E(samples%5Bi%5D+*+0.999f)%3B%0A+++//std::transform(samples,+samples%2B4096,+samples,+%5B%5D(auto+samp)+%7Breturn+samp+*+0.999f%3B+%7D)%3B%0A++++return+samples%5B777%5D%3B%0A%7D'),l:'5',n:'0',o:'C%2B%2B+source+%231',t:'0')),k:50,l:'4',n:'0',o:'',s:0,t:'0'),(g:!((h:compiler,i:(compiler:g7snapshot,filters:(b:'0',commentOnly:'0',directives:'0',intel:'0'),options:'-O3',source:1),l:'5',n:'0',o:'x86-64+gcc+7+(snapshot)+(Editor+%231,+Compiler+%231)',t:'0')),k:50,l:'4',n:'0',o:'',s:0,t:'0')),l:'2',n:'0',o:'',t:'0')),version:4
    */

#ifdef _DEBUG
    if (!(CAN_USE_SSE_ON(data)) || (elementCount % 8)) __debugbreak();
#endif

    for (size_t i = 0; i < elementCount; i += 8) {
        __m128i xmm0;
        __m128i xmm2;
        const __m128i xmm4{ 0 };


        _mm_store_si128(&xmm2, xmm4);
        __m128i xmm1 = _mm_load_si128(reinterpret_cast<__m128i*>(data + i));
        xmm2 = _mm_cmpgt_epi16(xmm2, xmm1);
        _mm_store_si128(&xmm0, xmm1);
        xmm1 = _mm_unpackhi_epi16(xmm1, xmm2);
        xmm0 = _mm_unpacklo_epi16(xmm0, xmm2);
        auto multPack1 = _mm_cvtepi32_ps(xmm1);
        auto multPack2 = _mm_cvtepi32_ps(xmm0);
        multPack1 = _mm_mul_ps(multPack1, multPack);
        multPack2 = _mm_mul_ps(multPack2, multPack);
        xmm1 = _mm_cvttps_epi32(multPack1);
        xmm0 = _mm_cvttps_epi32(multPack2);
        _mm_store_si128(&xmm2, xmm0);
        xmm0 = _mm_unpacklo_epi16(xmm0, xmm1);
        xmm2 = _mm_unpackhi_epi16(xmm2, xmm1);
        _mm_store_si128(&xmm1, xmm0);
        xmm0 = _mm_unpacklo_epi16(xmm0, xmm2);
        xmm1 = _mm_unpackhi_epi16(xmm1, xmm2);
        xmm0 = _mm_unpacklo_epi16(xmm0, xmm1);
        _mm_store_si128(reinterpret_cast<__m128i*>(data + i), xmm0);
    }
}

inline float helpers::parseArmaNumber(std::string_view armaNumber) {
    return static_cast<float>(std::strtof(armaNumber.data(),nullptr));
}

inline int helpers::parseArmaNumberToInt(std::string_view armaNumber) {
    return static_cast<int>(std::round(parseArmaNumber(armaNumber)));
}

bool helpers::startsWith(const std::string& shouldStartWith, const  std::string& startIn) {
    if (startIn.size() >= shouldStartWith.size()) {
        const auto res = std::mismatch(shouldStartWith.begin(), shouldStartWith.end(), startIn.begin());
        return (res.first == shouldStartWith.end());
    } else {
        return false;
    }
}

//http://stackoverflow.com/a/5506223
std::vector<std::string>& helpers::split(const std::string& s, char delim, std::vector<std::string>& elems) {
    std::string::size_type lastPos = 0;
    const std::string::size_type length = s.length();

    while (lastPos < length + 1) {
        std::string::size_type pos = s.find_first_of(delim, lastPos);
        if (pos == std::string::npos) {
            pos = length;
        }

        //if (pos != lastPos || !trimEmpty)
        elems.emplace_back(s.data() + lastPos, pos - lastPos);

        lastPos = pos + 1;
    }

    return elems;
}

std::vector<std::string_view>& helpers::split(std::string_view s, char delim, std::vector<std::string_view>& elems) {
    std::string::size_type lastPos = 0;
    const std::string::size_type length = s.length();

    while (lastPos < length + 1) {
        std::string::size_type pos = s.find_first_of(delim, lastPos);
        if (pos == std::string::npos) {
            pos = length;
        }

        //if (pos != lastPos || !trimEmpty)
        elems.emplace_back(s.data() + lastPos, pos - lastPos);

        lastPos = pos + 1;
    }

    return elems;
}

std::vector<std::string> helpers::split(const std::string& s, char delim) {
    std::vector<std::string> elems;
    split(s, delim, elems);
    return elems;
}

std::vector<std::string_view> helpers::split(std::string_view s, char delim) {
    std::vector<std::string_view> elems;
    split(s, delim, elems);
    return elems;
}

bool helpers::isTrue(std::string_view string) {
    if (string.length() != 4)//small speed optimization
        return string.length() == 1 && string.at(0) == '1';
    return string == "true";
}

short* helpers::allocatePool(int sampleCount, int channels, short* samples) {
    short* result = new short[sampleCount * channels];
    memcpy(result, samples, sampleCount * channels * sizeof(short));
    return result;
}

void helpers::mix(short* to, const short* from, int sampleCount, int channels) {//#TODO SSE2 implementation
    for (int q = 0; q < sampleCount * channels; q++) {
        int sum = static_cast<int>(to[q]) + static_cast<int>(from[q]);

        to[q] = std::clamp(sum, SHRT_MIN, SHRT_MAX);
    }
}

float helpers::volumeMultiplifier(const float volumeValue) {
    const auto normalized = (volumeValue + 1) / 10.0f;
    return pow(normalized, 4);
}

std::map<std::string, FREQ_SETTINGS, std::less<>> helpers::parseFrequencies(std::string_view string) {
    std::map<std::string, FREQ_SETTINGS, std::less<>> result;
    auto sub = string.substr(1, string.length() - 2);
    if (sub.empty()) return result;
    std::vector<std::string_view> v;
    v.reserve(4);
    split(sub, ',', v);
    for (std::string_view xs : v) {
        auto parts = split(xs.substr(1, xs.length() - 2), '|');
        if (parts.size() == 3 || parts.size() == 4) {
            FREQ_SETTINGS settings;
            settings.volume = parseArmaNumberToInt(parts[1]);
            settings.stereoMode = static_cast<stereoMode>(parseArmaNumberToInt(parts[2]));
            if (parts.size() == 4)
                settings.radioClassname = parts[3];
            result.insert_or_assign(std::string(parts[0]),settings);
        }
    }
    return result;
}

#include <utility>

vehicleDescriptor helpers::getVehicleDescriptor(std::string_view vehicleID) {
    vehicleDescriptor result;
    if (vehicleID.find("_turnout") != std::string::npos) {
        result.vehicleName = vehicleID.substr(0, vehicleID.find("_turnout"));
    } else {
        if (vehicleID.find_last_of('_') != std::string::npos) {
            result.vehicleName = vehicleID.substr(0, vehicleID.find_last_of('_'));
           
            result.vehicleIsolation = std::atof(vehicleID.substr(vehicleID.find_last_of('_') + 1).data()); //#TODO  std::from_chars()
        } else {
            result.vehicleName = vehicleID;
        }
    }
    return result;
}

float helpers::distanceForDiverRadio() {
    return DD_MIN_DISTANCE + (DD_MAX_DISTANCE - DD_MIN_DISTANCE) * (1.0f - TFAR::getInstance().m_gameData.wavesLevel); //#TODO WTF?! underwater range is influenced by wave intensity?
}
