import Ajv from 'ajv'
import addFormats from 'ajv-formats'

// https://ajv.js.org/options.html
const ajv = new Ajv({
    allErrors: true,
})

addFormats(ajv)

export default ajv
