import Ajv from 'ajv'

// https://ajv.js.org/options.html
export default new Ajv({
    allErrors: true,
})
