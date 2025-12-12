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
            from: 'resource',
            to: 'resource',
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
        // Required for SharedArrayBuffer (if using threads)
        'Cross-Origin-Opener-Policy': 'same-origin',
        'Cross-Origin-Embedder-Policy': 'require-corp',
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
