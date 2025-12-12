const path = require('path');
const HtmlWebpackPlugin = require('html-webpack-plugin');
const CopyWebpackPlugin = require('copy-webpack-plugin');

module.exports = {
  entry: './src/index.ts',
  target: 'web',
  module: {
    rules: [
      {
        test: /\.tsx?$/,
        use: 'ts-loader',
        exclude: /node_modules/,
      },
      {
        test: /\.wasm$/,
        type: 'asset/resource',
      },
    ],
  },
  resolve: {
    extensions: ['.tsx', '.ts', '.js', '.wasm'],
    fallback: {
      // Disable Node.js polyfills for browser-only build
      // These are not needed for the WASM loader when running in browser
      // If you need to support server-side rendering, enable appropriate polyfills
      "fs": false,
      "path": false,
      "crypto": false,
    }
  },
  output: {
    filename: 'bundle.js',
    path: path.resolve(__dirname, 'dist'),
    clean: true,
  },
  plugins: [
    new HtmlWebpackPlugin({
      template: './src/index.html',
      title: 'BespokeSynth WASM',
    }),
    new CopyWebpackPlugin({
      patterns: [
        {
          from: 'build/*.wasm',
          to: '[name][ext]',
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
  },
  experiments: {
    asyncWebAssembly: true,
  },
};
