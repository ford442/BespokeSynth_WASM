const path = require('path');
const HtmlWebpackPlugin = require('html-webpack-plugin');
const CopyWebpackPlugin = require('copy-webpack-plugin');

module.exports = (env, argv) => {
  const isDevelopment = argv.mode === 'development';
  // Opt-in COOP/COEP service worker for hosts that cannot send the headers (src/coi/).
  const coiServiceWorker = process.env.BESPOKE_COI_SW === '1' || Boolean(env && env.coiServiceWorker);

  return {
    entry: './src/index.ts',
    
    output: {
      path: path.resolve(__dirname, 'dist'),
      filename: 'bundle.[contenthash].js',
      clean: true,
    },

    resolve: {
      extensions: ['.ts', '.tsx', '.js', '.jsx'],
    },

    module: {
      rules: [
        {
          test: /\.tsx?$/,
          use: 'ts-loader',
          exclude: /node_modules/,
        },
        {
          test: /\.css$/,
          use: ['style-loader', 'css-loader'],
        },
      ],
    },

    plugins: [
      new HtmlWebpackPlugin({
        template: './src/index.html',
        inject: 'body',
        coiServiceWorker,
      }),
      new CopyWebpackPlugin({
        patterns: [
          {
            from: 'wasm/dist',
            to: 'wasm',
            noErrorOnMissing: true,
          },
          {
            from: 'src/audio/processors',
            to: 'audio',
            noErrorOnMissing: true,
          },
          ...(coiServiceWorker ? [{ from: 'src/coi/coi-serviceworker.js', to: 'coi-serviceworker.js' }] : []),
        ],
      }),
    ],

    devServer: {
      static: {
        directory: path.join(__dirname, 'dist'),
      },
      compress: true,
      port: 8080,
      hot: true,
      open: true,
      headers: {
        // Cross-origin isolation enables SharedArrayBuffer, which both AudioWorklet
        // backends need: the worklet engine (BespokeSynthWASM_audio, the default
        // whenever the page is isolated) and the SAB ring fallback (?audio=ring).
        // See src/audio/audioMode.ts. Non-isolated pages and ?audio=sdl get SDL2.
        // Production hosts must send the same COOP/COEP pair or they get SDL2.
        // `npm run serve` (scripts/e2e-static-server.js) sends them too.
        // BESPOKE_WASM_THREADS stays OFF: these headers do not enable pthreads.
        'Cross-Origin-Opener-Policy': 'same-origin',
        'Cross-Origin-Embedder-Policy': 'require-corp',
        // Worklet scripts loaded via addModule need CORP when COEP is require-corp.
        'Cross-Origin-Resource-Policy': 'same-origin',
      },
    },

    devtool: isDevelopment ? 'source-map' : false,

    performance: {
      hints: false,
      maxAssetSize: 10485760, // 10MB - WASM files can be large
      maxEntrypointSize: 10485760,
    },
  };
};
