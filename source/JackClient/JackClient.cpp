#include <JackClient.h>

JackClient::JackClient([[ maybe_unused ]] int argc, char *argv[]): m_host_audio_semaphore(1) {

    const char *server_name = NULL;
    int options = JackNullOption;

    parse_args(argc, argv);

    if (m_config_path.empty()) {
        std::cerr << "[error] Please provide a config file with --configfile <path/to/configfile>!" << std::endl;
        exit (EXIT_FAILURE);
    }

    if (m_client_name == nullptr) {
        m_client_name = strrchr ( argv[0], '/' );
        if ( m_client_name == nullptr ) {
            m_client_name = "jack_client";
        }
        else {
            m_client_name++;
        }
    }

    /* open a client connection to the JACK server */
    m_client = jack_client_open ( m_client_name, (jack_options_t) options, &m_status, server_name );
    if ( m_client == NULL ) {
        fprintf ( stderr, "[error] jack_client_open() failed, status = 0x%2.0x\n", m_status );
        if ( m_status & JackServerFailed )
        {
            fprintf ( stderr, "[error] Unable to connect to JACK server\n" );
        }
        exit ( 1 );
    }
    if ( m_status & JackServerStarted )
    {
        fprintf ( stderr, "[info] JACK server started\n" );
    }
    if ( m_status & JackNameNotUnique )
    {
        m_client_name = jack_get_client_name ( m_client );
        fprintf ( stderr, "[error] unique name `%s' assigned\n", m_client_name );
    }
    #ifdef VERSION
    std::cout << "[info] audio-matrix version " << VERSION << std::endl;
    #endif
    audio_matrix = new AudioMatrix(m_config_path);

    // create input and output ports
    size_t n_input_channels = audio_matrix->get_n_input_channels();
    size_t n_output_channels = audio_matrix->get_n_output_channels(); 

    input_ports = ( jack_port_t** ) calloc ( n_input_channels, sizeof ( jack_port_t* ) );
    output_ports = ( jack_port_t** ) calloc ( n_output_channels, sizeof ( jack_port_t* ) );

    for (int i = 0; i < n_input_channels; i++ ) {
        std::string port_name = "input_" + std::to_string(i);
        input_ports[i] = jack_port_register (m_client, port_name.c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        if( input_ports[i] == NULL )
        {
            fprintf ( stderr, "[error] no more JACK ports available\n" );
            exit ( 1 );
        }
    }

    for (int i = 0; i < n_output_channels; i++ ) {
        std::string port_name = audio_matrix->get_ouput_port_name(i);
        output_ports[i] = jack_port_register (m_client, port_name.c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0 );
        if( output_ports[i] == NULL ) {
            fprintf ( stderr, "[error] no more JACK ports available\n" );
            exit ( 1 );
        }
    }

    // tell the JACK server to call `process()' whenever there is work to be done.
    // before setting the process callback, we need to allocate the input and output ports. Why? this is not exactly clear to me. Is the method `process' called immediately after setting the process callback?
    jack_set_process_callback (m_client, process, this);

    // tell the JACK server to call `jack_shutdown()' if it ever shuts down, either entirely, or if it just decides to stop calling us.
    jack_on_shutdown (m_client, shutdown, this);


    m_nframes = jack_get_buffer_size(m_client);
    m_samplerate = jack_get_sample_rate(m_client);

    // registers a function to be called when the maximum buffer size changes
    if(verbose)
        std::cout << "[debug] registering buffer size callback" << std::endl;
    jack_set_buffer_size_callback(m_client, buffer_size_callback, this);

    // registers a function to be called when the sample rate changes
    // This function is also called directly after registering it, so we don't need to call prepare explicitely
    // std::cout << "[debug] registering sample rate callback" << std::endl;
    jack_set_sample_rate_callback(m_client, sample_rate_callback, this);

}

JackClient::~JackClient() {

    // jack_deactivate(m_client);
    
    int close_ret = jack_client_close(m_client);
    // ports need to be freed after the client is closed, otherwise we can run into setfaults
    // std::cout << "[debug] JackClient close return val: " << close_ret << std::endl;

    free (input_ports);
    free (output_ports);
    delete audio_matrix;
    delete[] in;
    delete[] out;
    std::cout << "[info] JackClient destructed!" << std::endl;
}

void JackClient::parse_args(int argc, char* argv[]) {

    int c;

    while (true) {
        int option_index                    = 0;
        static struct option long_options[] = {
            {"configfile", required_argument, nullptr, 'c'},
            {"jackclientname", required_argument, nullptr, 'j'},
            {"verbose", no_argument, nullptr, 'v'},
            {"version", no_argument, nullptr, 1},
            {"help", no_argument, nullptr, 'h'},
            {nullptr, 0, nullptr, 0}};

        c = getopt_long(argc, argv, "c:j:vh", long_options, &option_index);

        if (c == -1) { break; }

        switch (c) {
            case 1:
                #ifdef VERSION
                    std::cout << "audio-matrix version " << VERSION << std::endl;
                #else
                    std::cout << "audio-matrix version unknown" << std::endl;
                #endif
                exit(EXIT_SUCCESS);
                break;
            case 'c':
                m_config_path = optarg;
                break;

            case 'j':
                m_client_name = optarg;
                break;

            case 'h':
                printf(
                    "commandline arguments for audio-matix:\n"
                    "--configfile,          -c (path to the audio-matrix configuration file)\n"
                    "--jackclientname,      -j (name of the jack client)\n"
                    "--verbose,             -v (be more verbose)\n"
                    "--help,                -h \n");
                exit(EXIT_SUCCESS);

            case 'v':
                verbose = true;
                break;


            case '?':
                /* getopt_long already printed an error message. */
                break;

            default:
                printf("?? getopt returned character code 0%o ??\n", c);
                exit(EXIT_FAILURE);
        }
    }

    if (optind < argc) {
        printf("non-option ARGV-elements: ");

        while (optind < argc) { printf("%s ", argv[optind++]); }

        printf("\n");

        exit(EXIT_FAILURE);
    }
}

void JackClient::prepare() {
    prepare(HostAudioConfig(m_nframes, m_samplerate));
}

void JackClient::prepare(HostAudioConfig config) {
    // TODO: shall the client be deactivated before preparing?
    if(verbose)
        std::cout << "[debug] entering audio matrix prepare with HostAudioConfig buffer_size="  << config.m_host_buffer_size << ", sr=" << config.m_host_sample_rate << std::endl;
    audio_matrix->prepare(config);


    size_t n_input_channels = audio_matrix->get_n_input_channels();
    size_t n_output_channels = audio_matrix->get_n_output_channels(); 

    delete[] in;
    delete[] out;

    // allocate memory for the input and output buffers
    in = new jack_default_audio_sample_t*[n_input_channels];
    out = new jack_default_audio_sample_t*[n_output_channels];
    for (int i = 0; i < n_input_channels; i++ ) {
        in[i] = new jack_default_audio_sample_t[config.m_host_buffer_size];
    }
    for (int i = 0; i < n_output_channels; i++ ) {
        out[i] = new jack_default_audio_sample_t[config.m_host_buffer_size];
    }

    // Tell the JACK server that we are ready to roll. Our process() callback will start running now.
    if (jack_activate(m_client)) {
        fprintf ( stderr, "[error] cannot activate client" );
        exit ( 1 );
    } else {
        std::cout << "[info] jack client is ready" << std::endl;
    }
}

int JackClient::process(jack_nframes_t nframes, void *arg) {
    JackClient* jack_client = static_cast<JackClient*>(arg);

    for (int i = 0; i < jack_client->audio_matrix->get_n_input_channels(); i++ ) {
        jack_client->in[i] = (jack_default_audio_sample_t *) jack_port_get_buffer(jack_client->input_ports[i], nframes);
    }
    for (int i = 0; i < jack_client->audio_matrix->get_n_output_channels(); i++ ) {
        jack_client->out[i] = (jack_default_audio_sample_t *) jack_port_get_buffer (jack_client->output_ports[i], nframes);
    }

    jack_client->audio_matrix->process((float**) jack_client->in, (float**) jack_client->out, (size_t) nframes);

    return 0;
}

void JackClient::shutdown(void *arg) {
    std::cout << "[info] Jack server has shut down, exiting ..." << std::endl;
    delete static_cast<JackClient*>(arg);
}

int JackClient::buffer_size_callback(jack_nframes_t nframes, void *arg) {

    JackClient* jack_client = static_cast<JackClient*>(arg);

    // semaphore ensures that only one callback calls jack_client->prepare() at a time
    jack_client->m_host_audio_semaphore.acquire();
    
    jack_client->m_nframes = nframes;
    jack_client->prepare(HostAudioConfig(jack_client->m_nframes, jack_client->m_samplerate));
    std::cout << "[info] Buffer size changed to " << nframes << std::endl;
    
    jack_client->m_host_audio_semaphore.release();
    return 0;
}

int JackClient::sample_rate_callback(jack_nframes_t nframes, void *arg) {
    
    JackClient* jack_client = static_cast<JackClient*>(arg);

    // semaphore ensures that only one callback calls jack_client->prepare() at a time
    jack_client->m_host_audio_semaphore.acquire();

    jack_client->m_samplerate = nframes;
    jack_client->prepare(HostAudioConfig(jack_client->m_nframes, jack_client->m_samplerate));
    std::cout << "[info] Sample rate changed to " << nframes << std::endl;

    jack_client->m_host_audio_semaphore.release();
    
    return 0;
}
