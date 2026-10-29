const path = require('path');
const HtmlWebpackPlugin = require('html-webpack-plugin');
const CopyWebpackPlugin = require('copy-webpack-plugin');

module.exports = (env, argv) => {
  const isDevelopment = argv.mode === 'development';

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
      }),
      new CopyWebpackPlugin({
        patterns: [
          {
            from: 'wasm/dist',
            to: 'wasm',
            noErrorOnMissing: true,
          },
          {
            from: 'wasm/resource-pack',
            to: 'resource',
            noErrorOnMissing: true,
          },
          {
            from: 'src/audio/processors',
            to: 'audio',
            noErrorOnMissing: true,
          },
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
        // Cross-origin isolation enables SharedArrayBuffer for the AudioWorklet
        // SAB ring backend, which is the default whenever the page is isolated
        // (src/audio/audioMode.ts). SDL2 is the fallback for non-isolated pages
        // and for ?audio=sdl. Production hosts must send the same COOP/COEP pair
        // (or equivalent) or they get SDL2. `npm run serve` sends them too.
        // BESPOKE_WASM_THREADS stays OFF — these headers are not an enablement
        // of pthreads.
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
