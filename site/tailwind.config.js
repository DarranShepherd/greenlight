/** @type {import('tailwindcss').Config} */
module.exports = {
	content: ['./index.html', './scripts/**/*.js'],
	theme: {
		extend: {
			colors: {
				background: '#00120a',
				surface: '#00120a',
				'surface-container-lowest': '#000000',
				'surface-container-low': '#00170f',
				'surface-container': '#001e14',
				'surface-container-high': '#00251a',
				'surface-container-highest': '#002c1f',
				'surface-variant': '#002c1f',
				'on-background': '#ccfbe5',
				'on-surface': '#ccfbe5',
				'on-surface-variant': '#87b5a1',
				outline: '#537e6c',
				'outline-variant': '#245040',
				primary: '#f7ffba',
					'primary-container': '#eeff00',
				'on-primary-container': '#525c00',
				secondary: '#f7e54e',
				'secondary-container': '#695f00',
				tertiary: '#ffe390',
				error: '#ff7351',
					'super-cheap-green': '#73c73d',
					'cheap-green': '#62c0a3',
					'normal-yellow': '#f7ec4c',
					'expensive-orange': '#eb812b',
					'very-expensive-red': '#e43117'
			},
			fontFamily: {
				headline: ['Manrope', 'sans-serif'],
				body: ['Manrope', 'sans-serif']
			},
			borderRadius: {
				DEFAULT: '0.5rem',
				lg: '0.5rem',
				xl: '0.75rem',
				'2xl': '1rem',
				'3xl': '1.5rem',
				full: '9999px'
			}
		}
	},
	plugins: [require('@tailwindcss/forms')]
};
