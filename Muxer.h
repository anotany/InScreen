extern "C" 
{    
	
	#include <libavformat\avformat.h>
	#include <libavutil\mathematics.h>
	#include <libswscale\swscale.h>
}

#pragma comment(lib, "lib\\avcodec.lib")
#pragma comment(lib, "lib\\avformat.lib")
#pragma comment(lib, "lib\\swscale.lib")

class Muxer
{
private:
	AVFrame *frame;
	AVPicture src_picture, dst_picture;
	int frame_count;
private:
	void OpenVideosStream(void);
	void WriteVideoFrame(void);
	void WriteAudioFrame(void);
public:
	Muxer(void);
	~Muxer(void);
};

