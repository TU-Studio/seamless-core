#include <Track.h>

Track::Track(const TrackConfig& config): m_config(config) {

        for (const std::shared_ptr<ModuleConfig> module_config : m_config.modules){
            switch (module_config->module_type())
            {
            case Modules::GAIN:
                {
                    auto gain_config = std::dynamic_pointer_cast<GainConfig>(module_config);
                    Gain gain(gain_config);
                    std::shared_ptr<Gain> gain_ptr = std::make_shared<Gain>(gain);
                    m_modules.push_back(std::static_pointer_cast<Module>(gain_ptr));
                }
                break;
            default:
                break;
            }
        }
}

Track::~Track() {
}

void Track::prepare(HostAudioConfig host_audio_config) {
    m_buffer(m_max_number_of_channels, host_audio_config.m_host_buffer_size);
    for (auto module : m_modules) {
        module->prepare(host_audio_config);
    }
}

void Track::process(jack_nframes_t nframes) {
    for (auto& module : m_modules) {
        module->process(m_buffer, nframes);
    }
}

AudioBufferF& Track::get_buffer() {
    return m_buffer;
}