//
// Created by TK-V on 2021/12/24.
//

// mixing multiple PCM sound together
#include <string>
#include <vector>
#include <cmath>

#include <iostream>
#include <fstream>
#include <iomanip>

#include <thread>
#include <chrono>

#include "SDL.h"

#include "stdlib.h"
#include <WinSock2.h>

static  Uint8  *audio_chunk;
static  Uint32  audio_len;
static  Uint8  *audio_pos;

volatile int udpLoop = 0;
volatile int udpCount = 0;


//std::vector<std::string> file_paths = {R"(D:\Data\211222_TrafficNoise1\F01.pcm)",R"(D:\Data\211222_TrafficNoise1\F02.pcm)",
//                                       R"(D:\Data\211222_TrafficNoise1\F03.pcm)",R"(D:\Data\211222_TrafficNoise1\F04.pcm)",
//                                       R"(D:\Data\211222_TrafficNoise1\F05.pcm)",R"(D:\Data\211222_TrafficNoise1\F06.pcm)",
//                                       R"(D:\Data\211222_TrafficNoise1\F07.pcm)",R"(D:\Data\211222_TrafficNoise1\F08.pcm)",
//                                       R"(D:\Data\211222_TrafficNoise1\F09.pcm)",R"(D:\Data\211222_TrafficNoise1\F10.pcm)"};


char* file_names[] = {(char*)"L01",(char*)"L02",(char*)"L03",(char*)"L04",(char*)"L05",(char*)"L06",(char*)"L07",
                    (char*)"L08",(char*)"L09",(char*)"LTS",(char*)"R01",(char*)"R02",(char*)"R03",(char*)"R04",
                    (char*)"R05",(char*)"R06",(char*)"R07",(char*)"R08",(char*)"R09",(char*)"RTS",};



// Only support S16LSB PCM data now!
struct pcmRepeatFader{
    explicit pcmRepeatFader(std::string input_path, unsigned short pcm_channel = 1,
                            unsigned long fade_length = 44100)
            : path(input_path) , channel(pcm_channel), fade_buffer_size(fade_length)
            , file(std::ifstream(path, std::ios::binary)){
        if(!file){
            file_state = 0;
            std::cerr << path << " read failed!\n";
            return;
        }
        // get file size
        std::streampos begin, end;
        file.seekg(0, std::ios::end);
        end = file.tellg();
        file.seekg(0, std::ios::beg);
        begin = file.tellg();
        size = end - begin;
        fade_buffer_size = std::min(size/2, fade_buffer_size);
        // reset file pointer
        pos = 0;
        last_read = 0;
        // generate file fade buffer
        fade_buffer = new char[fade_buffer_size];
        file.seekg(size - fade_buffer_size, std::ios::beg);
        file.read(&fade_buffer[0], fade_buffer_size);
        file.seekg(0, std::ios::beg);
    }
    virtual ~pcmRepeatFader(){
        file.close();
        delete[] fade_buffer;
    }
    int fillExt(char* ext_buffer, unsigned long ext_size, unsigned short pcm_channel = 1){
        unsigned long pos_current = 0;
        while(pos_current < ext_size){
            unsigned long read_target = std::min(ext_size - pos_current, size - fade_buffer_size - pos);

            file.read(&ext_buffer[pos_current], read_target);
            last_read = file.gcount();
            fadeInterp(ext_buffer, pos_current);
            if(pos + last_read >= size - fade_buffer_size){
                fade_buffer_ok = 1;
                file.clear();
                file.seekg(0, std::ios::beg);
            }
            pos_current = pos_current + last_read;
            pos = file.tellg();
        }
        return 0;
    }

    int fadeInterp(char* ext_buffer, unsigned long pos_current){
        if(fade_buffer_ok == 0){
            return 0;
        }
        for(unsigned long idx = 0; idx < last_read; idx += bit_depth){
            unsigned long idx_file = pos + idx;
            if (idx_file >= fade_buffer_size) continue;
            unsigned long idx_buffer = pos_current + idx;
            unsigned long idx_fade = idx_file;
            int buffer_sample = (static_cast<char>(ext_buffer[idx_buffer + 1]) << 8) + static_cast<unsigned char>(ext_buffer[idx_buffer]);
            int fade_sample = (static_cast<char>(fade_buffer[idx_fade + 1]) << 8) + static_cast<unsigned char>(fade_buffer[idx_fade]);
            double kInterp = 1.0*idx_file/fade_buffer_size;
            short sample = kInterp * buffer_sample + (1 - kInterp) * fade_sample;
            ext_buffer[idx_buffer + 1] = sample >> 8;
            ext_buffer[idx_buffer] = sample & 0x00ff;
        }
        return 0;
    }

    unsigned long fade_buffer_size;
    std::string path;
    std::ifstream file;
    unsigned short channel;
    unsigned short bit_depth = 2;
    unsigned long size;
    unsigned long pos;
    unsigned long last_read;
    int file_state = 1;
    bool fade_buffer_ok = 0;
    char* fade_buffer;
};


// PCM data mixer
struct pcmMixer{
    pcmMixer(std::vector<std::string> file_lists, unsigned short input_ch = 1,
             unsigned short output_ch = 2, unsigned long fade_length = 44100,
             unsigned long pcm_buffer_length = 4096)
            : input_channel(input_ch), output_channel(output_ch),
              fade_buffer_size(fade_length), file_paths(file_lists),
              pcm_buffer_size(pcm_buffer_length), file_num(file_paths.size()){
        // initialize pcmRepeatFaders and data buffers
        if(file_paths.size()*output_channel > 200){
            std::cerr <<  "too many channels and files!\n";
            file_state = 0;
            return;
        }
        for(int i = 0; i < file_num; ++i){
            file_src[i] = new pcmRepeatFader(file_paths[i], input_channel, fade_buffer_size);
            if(file_src[i] -> file_state == 0) {
                std::cerr <<  "mix failed!\n";
                file_state = 0;
                return;
            }
            file_buffer[i] = new char[pcm_buffer_size];
        }
        mix_buffer_size = pcm_buffer_size / input_channel * output_channel;
        mix_buffer1 = new char[mix_buffer_size];
        mix_buffer2 = new char[mix_buffer_size];
        // initialize channel gains
        for(int i = 0; i < file_num*output_channel; ++i){
            mix_db[i] = gain2db(0.3 / ((i % file_num) * 5 + 1) / file_paths.size());
            mix_db_new[i] = gain2db(0.3 / ((i % file_num) * 5 + 1) / file_paths.size());
            if ((i+1) % 10 == 0){
                mix_db[i] = -120;
                mix_db_new[i] = -120;
            }
        }
        mix_db_ok = 1;
        // first file mix
        fileMix();
    }
    virtual ~pcmMixer(){
        delete[] mix_buffer1;
        delete[] mix_buffer2;
        for(int i = 0; i < file_paths.size(); ++i){
            delete file_src[i];
            delete[] file_buffer[i];
        }
    }
    static double db2gain(char vol){
        double gain = std::exp(std::log(10)*vol/20);
        return gain;
    }
    static char gain2db(double gain){
        char vol = std::max(std::min(std::round(std::log(gain)/std::log(10)*20), 10.0), -120.0);
        return vol;
    }
    int fileBuff() volatile {
        for(int i = 0; i < file_num; ++i){
            file_src[i] -> fillExt(file_buffer[i], pcm_buffer_size);
        }
        return 0;
    }
    short sampleMix(unsigned long posL, unsigned long idx, unsigned short ch) volatile {
        unsigned long posH = posL + 1;
        double result = 0;
        for(int i = 0; i < file_num; ++i) {
            int sample = (static_cast<char>(file_buffer[i][posH]) << 8)
                         + static_cast<unsigned char>(file_buffer[i][posL]);
            result += (db2gain(mix_db_old[ch*file_num+i]) * (1.0 - (1.0 + idx)/pcm_buffer_size)
                    + db2gain(mix_db_new[ch*file_num+i]) * (1.0 + idx)/pcm_buffer_size) * sample;
        }
        return static_cast<short>(std::max(std::min(result, 32767.0), -32768.0));
    }
    int fileMix() volatile {
        fileBuff();
        for(int i = 0; i < file_num*output_channel; ++i)
            mix_db_old[i] = mix_db_new[i];
        for(int i = 0; i < file_num*output_channel; ++i)
            mix_db_new[i] = mix_db[i];
        volatile char* buf;
        if(mix_buffer_current == 1){
            buf = mix_buffer1;
        }
        else{ //mix_buffer_current == 2
            buf = mix_buffer2;
        }
        unsigned long input_idx = 0;
        unsigned short input_no = 0;
        unsigned short output_no = 0;
        int k = 0;
        for(int i = 0; i < mix_buffer_size; i += bit_depth){
            if(output_no - k*input_channel >= input_channel){
                k = k + 1;
            }
            // if(k == 0){
                unsigned long posL = input_idx*input_channel*bit_depth + input_no*bit_depth;
                short sample = sampleMix(posL, input_idx, output_no);
                buf[i] = sample & 0x00ff;
                buf[i + 1] =  sample >> 8;
            // }
            // else{
            //     buf[i] = buf[i - k*input_channel*bit_depth];
            //     buf[i + 1] = buf[i + 1 - k*input_channel*bit_depth];
            // }
            ++input_no;
            if(input_no >= input_channel) {
                input_no = 0;
            }
            ++output_no;
            if(output_no >= output_channel) {
                input_no = 0;
                input_idx ++;
                output_no = 0;
                k = 0;
            }
        }
        mix_buffer_ok = 1;
        return 0;
    }
    int provideBuff(Uint8*& audio_chunk, Uint32& audio_len, Uint8*& audio_pos) volatile {
        while(mix_buffer_ok != 1);
        if(mix_buffer_current == 1) {
            //Set audio buffer (PCM data)
            audio_chunk = (Uint8*) mix_buffer1;
            //Audio buffer length
            audio_len = mix_buffer_size;
            audio_pos = audio_chunk;
            //Switch to another buffer
            mix_buffer_current = 2;
            mix_buffer_ok = 0;
        }
        else{ //mix_buffer_current == 2
            //Set audio buffer (PCM data)
            audio_chunk = (Uint8*) mix_buffer2;
            //Audio buffer length
            audio_len = mix_buffer_size;
            audio_pos = audio_chunk;
            //Switch to another buffer
            mix_buffer_current = 1;
            mix_buffer_ok = 0;
        }
        return 0;
    }
    unsigned short input_channel;
    unsigned short output_channel;
    unsigned short bit_depth = 2;
    unsigned long fade_buffer_size;
    unsigned long pcm_buffer_size;
    unsigned long mix_buffer_size;
    std::vector<std::string> file_paths;
    pcmRepeatFader* file_src[100];
    char* file_buffer[100];
    int file_num;
    int file_state = 1;
    int mix_buffer_current = 1;
    int mix_buffer_ok = 0;
    volatile char* mix_buffer1;
    volatile char* mix_buffer2;
    volatile char mix_db[200];
    volatile char mix_db_old[200];
    volatile char mix_db_new[200];
    int mix_db_ok = 0;
};

struct udpRcv{
    udpRcv(char* addr_in, int port_in, signed char* protect_wd= (signed char*)"protect_wd", char* ext = nullptr, int ext_l = 0)
            : addr1(addr_in), port1(port_in), extBuf(ext), extLen(ext_l), pr(protect_wd){
        // receive
        len = sizeof(SOCKADDR_IN);
    }
    virtual ~udpRcv(){
        udpCount = 0;
        WSACleanup();
        if(err == 0 && err2 == 0) {
            close();
        }
    }
    int open(){
        // load WinSock2
        wVersionRequested = MAKEWORD(2, 2);
        err = WSAStartup(wVersionRequested, &wsaData);
        err2 = LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2;
        if(err == 0 && err2 == 0){
            udpReady = 1;
        }
        else{
            std::cerr << "UDP Communication Error!" << std::endl;
            return -1;
        }
        sockSrv = socket(AF_INET, SOCK_DGRAM, 0);
        // bind Socket
        addrSrv.sin_addr.S_un.S_addr = inet_addr(addr1);
        addrSrv.sin_family = AF_INET;
        addrSrv.sin_port = htons(port1);
        bind(sockSrv, (SOCKADDR*)&addrSrv, sizeof(SOCKADDR));
        return 0;
    }
    void close() const{
        closesocket(sockSrv);
    }
    int receive(){
        int udpRcvSuccess = 0;
        if(udpReady == 1 && udpLoop == 1){
            if((nbSize = recvfrom(sockSrv, recvBuf, 100, 0, (SOCKADDR*)&addrClient, &len)) == SOCKET_ERROR){
                std::cerr << "UDP Receive Error!" << std::endl;
            }
            else{
                udpCount = udpCount + 1;
                udpRcvSuccess = 1;
                if(extBuf != nullptr && extLen!= 0){
                    int jump = 0;
                    int flag = 1;
                    while(pr[jump] != '\0') ++jump;
                    for(int i = 0; i < jump; ++i){
                        if(recvBuf[i] != pr[i])
                            flag = 0;
                    }
                    if(flag != 0) {
                        int fillLen = extLen; // std::min(extLen, len - jump);
                        for (int i = 0; i < fillLen; ++i) {
                            extBuf[i] = recvBuf[i + jump];
                        }
                    }
                }
            }
        }
        return udpReady && udpRcvSuccess;
    }
    int operator() (){
        int state = 0;
        open();
        if(err == 0 && err2 == 0) {
            while (udpLoop != 0) { //udpLoop == 1
                if (udpLoop == 1) {
                    state = receive();
                }
            }
        }
        close();
        return state;
    }
    WORD wVersionRequested;
    WSADATA wsaData;
    SOCKET sockSrv;
    SOCKADDR_IN addrSrv;
    SOCKADDR_IN addrClient;
    char* addr1;
    int port1;
    char recvBuf[100];
    int len;
    char* extBuf;
    signed char* pr;
    int extLen;
    int nbSize;
    int err = 1, err2;
    volatile int udpReady = 0;
};

struct iosDis{
    iosDis(char* gain, char** name, unsigned short ch, volatile int* udp_count)
            : gain_list(gain), channel_name(name), channel_num(ch), udp_num(udp_count){
        //system("chcp 65001");
    }
    void disBar(short n){
        n = std::max(n + 50, 0);
        for(int i = 0; i < n-1; ++i){
            std::cout << '-';
        }
        std::cout << 'I' ;
        for(int i = n; i < 79; ++i){
            std::cout << " ";
        }
        std::cout << "\n";
    }
    void disDot(unsigned long n){
        unsigned short edge = n % 41;
        for(int i = 0; i < edge; ++i){
            std::cout << '>';
        }
        for(int i = edge; i < 51; ++i){
            std::cout << ' ';
        }
    }
    void clearScreen(){
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        COORD coordScreen = { 0, 0 };    // home for the cursor
        SetConsoleCursorPosition( hConsole, coordScreen );
    }
    void refresh(){
        clearScreen();// system("cls");//clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::cout << "Vehicle Road Noise Simulator V0.0.2 20211228 xiataokai \n";
        for(int i = 0; i < channel_num; ++i){
            if(i % 10 == 0) std::cout << "\n";
            std::cout << std::right << std::setw(5) << channel_name[i];
            std::cout << std::right << std::setw(6) << static_cast<int>(gain_list[i]) << " dBFS ";
            disBar(gain_list[i]);
        }
        std::cout << "\n";
        std::cout << "  127.0.0.1:8351 UDP ";
        disDot(*udp_num);
        std::cout << "\n\n" << std::endl;
    }
    wchar_t d = L'█';
    wchar_t f = L'░';
    wchar_t comm = L'·';
    char* gain_list;
    char** channel_name;
    unsigned short channel_num;
    volatile int* udp_num;
};


void  fill_audio(void *udata, Uint8 *stream, int len) {
    //SDL 2.0
    SDL_memset(stream, 0, len);
    if (audio_len == 0)
        return;
    len = (len > audio_len ? audio_len : len);

    SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
    audio_pos += len;
    audio_len -= len;
}


int main(int argv, char** args) {  // 不加argv和args就会报错！
    // path finding
    std::string path0(args[0]);
    size_t pos0 = path0.rfind('\\');
    size_t pos1 = path0.rfind('/');
    size_t pos = 0;
    if(pos0 == -1){
        if(pos1 == -1)
            return -1;
        else
            pos = pos1;
    }
    else{
        if(pos1 == -1)
            pos = pos0;
        else
            pos = std::max(pos0, pos1);
    }
    path0 = path0.substr(0, pos);
    std::vector<std::string> file_paths = {path0 + R"(\pcm_data\NF01.pcm)", path0 + R"(\pcm_data\NF02.pcm)",
                                           path0 + R"(\pcm_data\NF03.pcm)", path0 + R"(\pcm_data\NF04.pcm)",
                                           path0 + R"(\pcm_data\NF05.pcm)", path0 + R"(\pcm_data\NF06.pcm)",
                                           path0 + R"(\pcm_data\NF07.pcm)", path0 + R"(\pcm_data\NF08.pcm)",
                                           path0 + R"(\pcm_data\NF09.pcm)", path0 + R"(\pcm_data\TS.pcm)"};

    // Pre-init
    std::cout.sync_with_stdio(false);
    std::cout << "SDL hello world" << std::endl;

    //Init
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        std::cout << "Could not initialize SDL - " << SDL_GetError() << "\n";
    }

    //SDL_AudioSpec
    SDL_AudioSpec wanted_spec;
    wanted_spec.freq = 44100;
    wanted_spec.format = AUDIO_S16LSB;
    wanted_spec.channels = 2;
    wanted_spec.silence = 0;
    wanted_spec.samples = 1024;
    wanted_spec.callback = fill_audio;

    if (SDL_OpenAudio(&wanted_spec, NULL) < 0) {
        std::cout << "can't open audio.\n";
    }

    // New data structure 3
    volatile pcmMixer mix0(file_paths);
    udpRcv *rcv0;
    rcv0 = new udpRcv((char *) "127.0.0.1", 8351, (signed char *) "rd1sd000",
                      (char *) mix0.mix_db, mix0.file_num * mix0.output_channel);
    iosDis display1(const_cast<char *>(mix0.mix_db), file_names, mix0.file_num * mix0.output_channel,
                    &udpCount);

    udpLoop = 1;
    std::thread t(*rcv0); t.detach();


    //Play
    SDL_PauseAudio(0);

    while (1) {
        mix0.provideBuff(audio_chunk, audio_len, audio_pos);

        auto disThread = [&]() mutable {display1.refresh();};
        std::thread dt (disThread);
        dt.detach();

        auto mixThread = [&]() mutable {mix0.fileMix();};
        std::thread dt2(mixThread);
        dt2.detach();

        while (audio_len > 0)//Wait until finish
            SDL_Delay(1);
    }
    SDL_Quit();

    udpLoop = 0;
    delete rcv0;

}
